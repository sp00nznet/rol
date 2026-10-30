#!/usr/bin/env python3
"""Rise of Legends .big archive + bxml (binary XML) tool.

  bigtool.py list    <file.big>
  bigtool.py extract <file.big> <outdir> [glob]   # bxml -> .xml (+ raw .bxml); raw twin -> .src
  bigtool.py bxml2xml <in.bxml> [out.xml]
  bigtool.py xml2bxml <in.xml> <out.bxml>
  bigtool.py replace <in.big> <out.big> <entry-path> <file>   # file may be .xml (encoded to bxml)
  bigtool.py selftest <file.big>                              # bxml + big round-trip check

.big ("WAR-BUILDER", version 11), little-endian; str = u32 nchars + UTF-16LE (no NUL);
list = u32 n, and if n: u32 capacity + bytes FF FF 00, then n records.
  u32 version(11) | u8 1 | str "WAR-BUILDER"
  list deps    : str path, u32 unix_time
  list entries : str path, 8 zero bytes, u32 offset, u32 size, u32 0, u32 unix_time,
                 str type ("bxml", "" raw, or a binary builder name), u8 a, u8 b
  at offset    : u32 zsize + zlib stream (inflates to `size` bytes)

bxml: u32 version(1) then five lists:
  nodes  12 B : u16 parent, u16 first_child, u16 next_sibling, u16 nattr, u32 first_attr
  attrs   8 B : u32 key = type<<28 | value_idx<<12 | name_idx, u32 num
                row 0 of a node is its tag; its value (if any) is the element text.
                types: 0 empty, 1 bool, 2 hex u32, 3 negative int, 4 uint, 5 float
                (num = f32 bits), 6 string, 7 binary (num = offset into blob list;
                element has dt:dt="bin.base64"). value_idx -> values (text form kept).
  names  str  : tag/attribute names
  values str  : values as text; [0] is ""
  blob   u8   : u32 len + bytes per binary element
Node order is a stack DFS: pop a node, append all its children, push them in order.
"""
import base64, collections, fnmatch, os, re, struct, sys, zlib
import xml.etree.ElementTree as ET

MARK = b"\xff\xff\x00"


class R:
    def __init__(s, d, p=0): s.d, s.p = d, p
    def u32(s):
        v = struct.unpack_from("<I", s.d, s.p)[0]; s.p += 4; return v
    def str(s):
        n = s.u32(); v = s.d[s.p:s.p + 2 * n].decode("utf-16le"); s.p += 2 * n; return v
    def list(s):
        n = s.u32()
        if n:
            s.u32()
            assert s.d[s.p:s.p + 3] == MARK, f"bad list marker at {s.p:#x}"
            s.p += 3
        return n


def wstr(v): return struct.pack("<I", len(v)) + v.encode("utf-16le")
def wlist(n): return struct.pack("<II", n, n) + MARK if n else struct.pack("<I", 0)


# ---------------- .big ----------------
def read_big(path):
    d = open(path, "rb").read()
    r = R(d)
    ver = r.u32(); flag = d[r.p]; r.p += 1; magic = r.str()
    assert magic == "WAR-BUILDER", magic
    deps = [(r.str(), r.u32()) for _ in range(r.list())]
    ents = []
    for _ in range(r.list()):
        e = {"path": r.str()}
        r.p += 8
        e["off"], e["size"], _z, e["time"] = struct.unpack_from("<IIII", d, r.p); r.p += 16
        e["type"] = r.str(); e["flags"] = d[r.p:r.p + 2]; r.p += 2
        ents.append(e)
    return {"ver": ver, "flag": flag, "deps": deps, "ents": ents, "data": d}


def entry_data(big, e):
    d = big["data"]; zs = struct.unpack_from("<I", d, e["off"])[0]
    out = zlib.decompressobj().decompress(d[e["off"] + 4:e["off"] + 4 + zs])  # a few streams lack the adler trailer
    if len(out) != e["size"]:  # multiplayer_data.big texture_cache.xml ships truncated
        print(f"warning: {e['path']}: {len(out)} of {e['size']} bytes", file=sys.stderr)
    return out


def write_big(big, payloads, out):
    """payloads: list of raw (uncompressed) bytes, parallel to big['ents']."""
    def header(offs):
        h = struct.pack("<I", big["ver"]) + bytes([big["flag"]]) + wstr("WAR-BUILDER")
        h += wlist(len(big["deps"])) + b"".join(wstr(p) + struct.pack("<I", t) for p, t in big["deps"])
        h += wlist(len(big["ents"]))
        for e, o, pl in zip(big["ents"], offs, payloads):
            h += wstr(e["path"]) + bytes(8) + struct.pack("<IIII", o, len(pl), 0, e["time"])
            h += wstr(e["type"]) + e["flags"]
        return h
    blobs = [zlib.compress(pl, 9) for pl in payloads]
    hlen = len(header([0] * len(blobs)))  # header size does not depend on offsets
    offs, o = [], hlen
    for b in blobs:
        offs.append(o); o += 4 + len(b)
    with open(out, "wb") as f:
        f.write(header(offs))
        for b in blobs: f.write(struct.pack("<I", len(b)) + b)


# ---------------- bxml ----------------
B64 = "bin.base64"


def bxml_to_tree(d):
    r = R(d); assert r.u32() == 1
    nodes = [struct.unpack_from("<HHHHI", d, r.p + 12 * i) for i in range(r.list())]; r.p += 12 * len(nodes)
    attrs = [struct.unpack_from("<II", d, r.p + 8 * i) for i in range(r.list())]; r.p += 8 * len(attrs)
    names = [r.str() for _ in range(r.list())]
    vals = [r.str() for _ in range(r.list())]
    n = r.list(); blob = d[r.p:r.p + n]

    def build(i):
        _par, child, _nxt, na, fa = nodes[i]
        k, num = attrs[fa]
        el = ET.Element(names[k & 0xFFF])
        if k >> 28 == 7:  # binary element text: num = offset of (u32 len + bytes) in blob
            ln = struct.unpack_from("<I", blob, num)[0]
            el.text = base64.b64encode(blob[num + 4:num + 4 + ln]).decode()
        elif (k >> 12) & 0xFFFF:
            el.text = vals[(k >> 12) & 0xFFFF]  # element text rides on the tag row
        for k, _num in attrs[fa + 1:fa + na]:
            el.set(names[k & 0xFFF], vals[(k >> 12) & 0xFFFF])
        while child != 0xFFFF:
            el.append(build(child)); child = nodes[child][2]
        return el
    return build(0)


INT = re.compile(r"\d+\Z")
NEG = re.compile(r"-\d+\Z")
HEX = re.compile(r"0x[0-9a-fA-F]{1,8}\Z")
FLOAT = re.compile(r"[+-]?(\d+\.\d*|\.\d+|\d+)([eE][+-]?\d+)?f?\Z")


def typed(v):
    """(type, num) for a value string, mirroring what the shipped files use."""
    if not v: return 0, 0
    if v in ("true", "false"): return 1, int(v == "true")
    if HEX.match(v): return 2, int(v, 16)
    if NEG.match(v) and int(v) >= -2**31: return 3, int(v) & 0xFFFFFFFF
    if INT.match(v) and int(v) < 2**32: return 4, int(v)
    if FLOAT.match(v):  # builder truncates toward zero, not round-to-nearest
        x = float(v.rstrip("f")); b = struct.unpack("<I", struct.pack("<f", x))[0]
        return 5, b - 1 if abs(struct.unpack("<f", struct.pack("<I", b))[0]) > abs(x) else b
    return 6, 0


def tree_to_bxml(root):
    order, kids, stack = [root], {}, [root]
    while stack:  # stack DFS, matches the shipped layout
        el = stack.pop(); kids[id(el)] = list(el)
        order += kids[id(el)]; stack += kids[id(el)]
    idx = {id(e): i for i, e in enumerate(order)}
    parent = {id(c): idx[id(p)] for p in order for c in p}
    names, vals = {}, {"": 0}  # value 0 is the empty string (tag rows point at it)
    intern = lambda tab, s: tab.setdefault(s, len(tab))
    nodes, attrs, blob = [], [], bytearray()
    for el in order:
        fa = len(attrs)
        if el.get("dt:dt") == B64:
            raw = base64.b64decode(el.text or "")
            attrs.append((7 << 28 | intern(names, el.tag), len(blob)))  # ponytail: value_idx 0; loader ignores it
            blob += struct.pack("<I", len(raw)) + raw
            items = list(el.attrib.items())
        else:
            text = el.text if el.text and (len(el) == 0 or el.text.strip()) else ""
            items = [(el.tag, text)] + list(el.attrib.items())
        for k, v in items:
            n, vi = intern(names, k), intern(vals, v)
            t, num = typed(v)
            attrs.append((t << 28 | (vi if t else 0) << 12 | n, num))
        k = kids[id(el)]
        nodes.append([parent.get(id(el), 0xFFFF), idx[id(k[0])] if k else 0xFFFF, 0xFFFF, len(attrs) - fa, fa])
    for el in order:
        k = kids[id(el)]
        for a, b in zip(k, k[1:]): nodes[idx[id(a)]][2] = idx[id(b)]
    assert len(nodes) < 0xFFFF and len(names) < 0x1000 and len(vals) < 0x10000
    out = struct.pack("<I", 1) + wlist(len(nodes)) + b"".join(struct.pack("<HHHHI", *n) for n in nodes)
    out += wlist(len(attrs)) + b"".join(struct.pack("<II", *a) for a in attrs)
    out += wlist(len(names)) + b"".join(map(wstr, names))
    out += wlist(len(vals)) + b"".join(map(wstr, vals))
    return out + wlist(len(blob)) + bytes(blob)


def parse_xml(path):
    """ET without namespace processing: bxml keeps 'xmlns:dt' / 'dt:dt' / 'xml:space' as plain names."""
    txt = open(path, encoding="utf-8").read()
    root = ET.fromstring(re.sub(r"([\s<])([A-Za-z_][\w.-]*):(?=[A-Za-z_])", r"\1\2__COLON__", txt))
    fix = lambda s: s.replace("__COLON__", ":") if s else s
    for el in root.iter():
        el.tag, el.text = fix(el.tag), fix(el.text)
        el.attrib = {fix(k): fix(v) for k, v in el.attrib.items()}
    return root


def tree_to_text(root):
    ET.indent(root)
    return ET.tostring(root, encoding="unicode")


# ---------------- cli ----------------
def main(a):
    cmd = a[0] if a else "help"
    if cmd == "list":
        big = read_big(a[1])
        for p, t in big["deps"]: print(f"dep  {p}  t={t}")
        for e in big["ents"]: print(f"{e['size']:10d}  {e['type']:<16} {e['path']}")
    elif cmd == "extract":
        big = read_big(a[1]); pat = a[3] if len(a) > 3 else "*"
        dupes = collections.Counter(e["path"].lower() for e in big["ents"])
        for e in big["ents"]:
            if not fnmatch.fnmatch(e["path"].lower(), pat.lower()): continue
            rel = e["path"].replace("\\", "/").lstrip("./") or e["type"]
            dst = os.path.join(a[2], rel)
            if e["type"] not in ("", "bxml"): dst += "." + e["type"]
            if e["type"] == "" and dupes[e["path"].lower()] > 1: dst += ".src"  # mod_data ships source + bxml
            os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
            raw = entry_data(big, e)
            if e["type"] == "bxml":
                open(dst + ".bxml" if not dst.endswith(".bxml") else dst, "wb").write(raw)
                open(dst, "w", encoding="utf-8").write(tree_to_text(bxml_to_tree(raw)))
            else:
                open(dst, "wb").write(raw)
            print(dst)
    elif cmd == "bxml2xml":
        txt = tree_to_text(bxml_to_tree(open(a[1], "rb").read()))
        (open(a[2], "w", encoding="utf-8").write(txt) if len(a) > 2 else print(txt))
    elif cmd == "xml2bxml":
        open(a[2], "wb").write(tree_to_bxml(parse_xml(a[1])))
    elif cmd == "replace":
        big = read_big(a[1]); want = a[3].lower()
        pls = [entry_data(big, e) for e in big["ents"]]
        hit = [i for i, e in enumerate(big["ents"]) if e["path"].lower() == want]
        assert hit, f"{a[3]} not in {a[1]}"
        new = open(a[4], "rb").read()
        if big["ents"][hit[0]]["type"] == "bxml" and a[4].lower().endswith(".xml"):
            new = tree_to_bxml(parse_xml(a[4]))
        pls[hit[0]] = new
        write_big(big, pls, a[2]); print(f"wrote {a[2]}")
    elif cmd == "selftest":
        big = read_big(a[1]); pls = [entry_data(big, e) for e in big["ents"]]
        same = tot = 0
        for e, raw in zip(big["ents"], pls):
            if e["type"] != "bxml": continue
            t = bxml_to_tree(raw); enc = tree_to_bxml(t); tot += 1
            same += enc == raw
            assert tree_to_text(bxml_to_tree(enc)) == tree_to_text(bxml_to_tree(raw)), e["path"]
        tmp = a[1] + ".selftest.tmp"
        write_big(big, pls, tmp); b2 = read_big(tmp)
        assert [entry_data(b2, e) for e in b2["ents"]] == pls
        os.remove(tmp)
        print(f"ok: big round-trips; bxml {tot} semantic-equal, {same} byte-identical")
    else:
        print(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
