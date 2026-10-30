#!/usr/bin/env python3
"""Build the archives the F10 menu's camera zoom swaps in.

    py -3 tools/make_enhanced_data.py <game dir> <out dir>

For each zoom level (far = 1.5x, farthest = 2x) it rewrites, from the
player's own install:

  .\\data\\gamecamera.xml       (multiplayer_data.big, base and patch8):
      more zoom levels out to a farther maximum, same step size
  .\\art\\terrain\\tilesets\\default_tilesets.xml   (art.big, base and patch8):
      fog pushed out by the same factor, or the far view is fog

into <out dir>/<level>/BIGS/<same path>. The host (enhance.c) opens those in
place of the originals when the zoom setting asks for them; the install is
never touched. Loose files do not override archives (the loader checks the
archives first), hence whole archives.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bigtool  # noqa: E402
import re  # noqa: E402

FOG_RE = re.compile(r'(fog_start|fog_end)="([^"]+)"')

LEVELS = {"far": 1.5, "farthest": 2.0}
CAMERA = ".\\data\\gamecamera.xml"
TILESETS = ".\\art\\terrain\\tilesets\\default_tilesets.xml"
ARCHIVES = {  # archive (relative to BIGS) -> entry it carries
    "multiplayer_data.big": CAMERA, "patch8\\multiplayer_data.big": CAMERA,
    "art.big": TILESETS, "patch8\\art.big": TILESETS,
}


def fnum(x):
    return "%g." % x if float(x).is_integer() else "%g" % x


def camera(xml, k):
    """REGULATION: keep the ~786-unit zoom step, extend the far end by k."""
    root = bigtool.ET.fromstring(xml)
    reg = root.find(".//REGULATION")
    near = float(reg.get("regulation_zoom_in_dist"))
    far = float(reg.get("regulation_zoom_out_dist"))
    levels = int(reg.get("regulation_num_zoom_levels"))
    step = (far - near) / (levels - 1)
    new_far = far * k
    reg.set("regulation_zoom_out_dist", fnum(new_far))
    reg.set("regulation_num_zoom_levels", str(round((new_far - near) / step) + 1))
    reg.set("regulation_extended_zoom_out_dist",
            fnum(float(reg.get("regulation_extended_zoom_out_dist")) * k))
    reg.set("regulation_zoom_out_pan_factor",
            fnum(float(reg.get("regulation_zoom_out_pan_factor")) * k))
    return bigtool.ET.tostring(root, encoding="utf-8")


def fog(tree, k):
    n = 0
    for el in tree.iter():
        for a in ("fog_start", "fog_end"):
            if el.get(a):
                el.set(a, fnum(float(el.get(a).rstrip("f")) * k)); n += 1
    return n


def build(game, out):
    bigs = os.path.join(game, "BIGS")
    for level, k in LEVELS.items():
        for rel, entry in ARCHIVES.items():
            src = os.path.join(bigs, rel)
            if not os.path.exists(src):
                continue
            big = bigtool.read_big(src)
            pls = [bigtool.entry_data(big, e) for e in big["ents"]]
            hit = [i for i, e in enumerate(big["ents"]) if e["path"].lower() == entry.lower()]
            if not hit:
                continue
            i = hit[0]
            if entry == CAMERA:
                pls[i] = camera(pls[i], k)
            elif big["ents"][i]["type"] == "bxml":
                tree = bigtool.bxml_to_tree(pls[i])
                assert fog(tree, k), f"no fog values in {rel}"
                pls[i] = bigtool.tree_to_bxml(tree)
            else:   # plain XML: edit the numbers in place, keep the rest byte for byte
                pls[i], n = FOG_RE.subn(lambda m: '%s="%s"' % (m[1], fnum(float(m[2].rstrip("f")) * k)),
                                        pls[i].decode("utf-8"))
                assert n, f"no fog values in {rel}"
                pls[i] = pls[i].encode("utf-8")
            dst = os.path.join(out, level, "BIGS", rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            bigtool.write_big(big, pls, dst)
            print(f"{level}: {dst}")


def selftest():
    xml = (b'<ROOT><DATA><REGULATION regulation_num_zoom_levels="8" regulation_zoom_in_dist="2000."'
           b' regulation_zoom_out_dist="7500." regulation_extended_zoom_out_dist="8600."'
           b' regulation_zoom_out_pan_factor="450."/></DATA></ROOT>')
    reg = bigtool.ET.fromstring(camera(xml, 2.0)).find(".//REGULATION")
    assert reg.get("regulation_zoom_out_dist") == "15000.", reg.attrib
    assert reg.get("regulation_num_zoom_levels") == "18", reg.attrib   # (15000-2000)/785.7 + 1
    t = bigtool.ET.fromstring('<a><b fog_start="3333." fog_end="9333."/></a>')
    assert fog(t, 1.5) == 2 and t.find("b").get("fog_end") == "13999.5"
    print("make_enhanced_data: ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    elif len(sys.argv) == 3:
        build(sys.argv[1], sys.argv[2])
    else:
        sys.exit(__doc__)
