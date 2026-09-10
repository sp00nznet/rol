#!/usr/bin/env python3
"""Harvest the strings that name Rise of Legends' own functions.

legends.exe is stripped, but it carries its own diagnostics: assert and log
strings spelling out real method names ("BHG::SoundManager::init") and some
full prototypes ("void __thiscall BHG::D3DVertStream::send_to_card(const int)").
Each one is referenced as a 4-byte immediate by the code it belongs to, so
finding the strings and then scanning for references to them turns a stripped
binary into a partial symbol map.

    python tools/symbols.py legends_unwrapped.exe -o config/symbols.json
    python tools/symbols.py --selftest

Feed it an unwrapped image -- a protected one has no readable .text.
"""

import argparse
import bisect
import json
import re
import struct
import sys

import pefile

STRING_RE = re.compile(rb'[\x20-\x7e]{6,400}\x00')
# Class::method, ~dtors and nested scopes included. The strings are a mix of
# bare names, full MSVC prototypes and assert prose that mentions the method;
# all three are useful, so pull the scoped token out of whatever it sits in.
SCOPE_RE = re.compile(r'\b[A-Za-z_]\w*(?:::[~A-Za-z_]\w*)+')


def sections(pe):
    """[(va, data)] for every section that has raw bytes."""
    base = pe.OPTIONAL_HEADER.ImageBase
    out = []
    for s in pe.sections:
        data = s.get_data()
        if data:
            out.append((base + s.VirtualAddress, data))
    return out


def find_named_strings(secs):
    """{va: (text, [scoped names])} for every string that names a symbol."""
    found = {}
    for va, data in secs:
        for m in STRING_RE.finditer(data):
            s = m.group()[:-1].decode('latin1')
            names = SCOPE_RE.findall(s)
            if names:
                found[va + m.start()] = (s, names)
    return found


def find_refs(secs, targets):
    """{target_va: [addresses holding a 4-byte pointer to it]}."""
    refs = {}
    for va, data in secs:
        for off in range(0, len(data) - 3):
            v = int.from_bytes(data[off:off + 4], 'little')
            if v in targets:
                refs.setdefault(v, []).append(va + off)
    return refs


def load_functions(path):
    """[(start, end, name)] sorted by start, from a disasm32 catalog."""
    with open(path) as f:
        cat = json.load(f)
    return sorted((fn['address'], fn['end'], fn.get('name')) for fn in cat['functions'])


def enclosing(funcs, addr):
    """The function containing addr, or None. funcs must be sorted."""
    i = bisect.bisect_right(funcs, (addr, float('inf'), None)) - 1
    if i < 0:
        return None
    start, end, name = funcs[i]
    return funcs[i] if start <= addr < end else None


def bind(strings, refs, funcs):
    """{function_start: {names}} -- every scoped name a function references."""
    named = {}
    for va, (_, names) in strings.items():
        for site in refs.get(va, []):
            fn = enclosing(funcs, site)
            if fn:
                named.setdefault(fn[0], set()).update(names)
    return named


def demo():
    """One runnable check: planted string, planted reference, both found."""
    base = 0x400000
    text = (b'BHG::SoundManager::init\x00'
            b'BigFile::load_header called on a file that is not inited.\x00'
            b'not a symbol, just prose here\x00')
    strings = find_named_strings([(base, text)])
    assert strings[base] == ('BHG::SoundManager::init', ['BHG::SoundManager::init'])
    # a name buried in assert prose still counts, and only the name is taken
    assert strings[base + 24][1] == ['BigFile::load_header'], strings[base + 24]
    assert len(strings) == 2, strings

    # push imm32 pointing at the string, inside a second section
    code = b'\x68' + struct.pack('<I', base) + b'\x90'
    refs = find_refs([(0x500000, code)], set(strings))
    assert refs == {base: [0x500001]}, refs

    # a reference that is only a coincidental partial match must not count
    assert find_refs([(0x500000, struct.pack('<I', base + 1))], set(strings)) == {}

    # the reference site binds to the function that encloses it, not a neighbour
    funcs = [(0x500000, 0x500010, None), (0x500010, 0x500020, None)]
    assert enclosing(funcs, 0x500001) == funcs[0]
    assert enclosing(funcs, 0x500010) == funcs[1]
    assert enclosing(funcs, 0x4fffff) is None
    assert enclosing(funcs, 0x500020) is None, 'past the last function is unmapped'
    assert bind(strings, refs, funcs) == {0x500000: {'BHG::SoundManager::init'}}
    print('selftest ok')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('exe', nargs='?', help='unwrapped legends.exe')
    ap.add_argument('-o', '--output', help='write JSON here')
    ap.add_argument('--functions', help='disasm32 catalog, to bind names to functions')
    ap.add_argument('--selftest', action='store_true')
    args = ap.parse_args()

    if args.selftest:
        demo()
        return 0
    if not args.exe:
        ap.error('give an exe, or --selftest')

    pe = pefile.PE(args.exe, fast_load=True)
    secs = sections(pe)
    strings = find_named_strings(secs)
    refs = find_refs(secs, set(strings))

    referenced = [va for va in strings if va in refs]
    unique = {n for _, names in strings.values() for n in names}
    print(f'{len(strings):6} strings naming a scoped symbol ({len(unique)} distinct names)')
    print(f'{len(referenced):6} of them referenced by code ({sum(len(r) for r in refs.values())} sites)')

    named = {}
    if args.functions:
        funcs = load_functions(args.functions)
        named = bind(strings, refs, funcs)
        certain = {a: next(iter(n)) for a, n in named.items() if len(n) == 1}
        print(f'{len(named):6} functions reference a scoped name '
              f'({len(certain)} of them exactly one -- take those as the symbol)')

    if args.output:
        out = {'strings': [{'va': va, 'text': strings[va][0], 'names': strings[va][1],
                            'refs': refs.get(va, [])} for va in sorted(strings)],
               'functions': {f'0x{a:08X}': sorted(n) for a, n in sorted(named.items())}}
        with open(args.output, 'w') as f:
            json.dump(out, f, indent=1)
        print(f'wrote {args.output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
