#!/usr/bin/env python3
"""Rise of Legends lift driver: work/functions.json -> src/recomp/gen/.

Drives pcrecomp's shared `tools/lift/generate.py` + `lift32` (pcrecomp#11),
the same shape as forcecommander's run_lift.py:

* **Closure-limited lifting** (`generate.closure`). Lift the call-graph closure
  from the OEP and give every other catalogued function a stub that aborts
  naming itself, so a run says exactly what to lift next instead of compiling
  ~12M instructions before anything has run once.
* **Extents by walking the branches** (`generate.true_extent`), capped by
  reach rather than the catalog's clamp; its docstring has why.

Every vtable slot RTTI names is injected as an entry (work/rtti_seeds.json):
MSVC's vtordisp adjustor thunks are reachable only through a vtable, so no
disassembler pass names them, and an unresolved slot answers eax = 0.

    py -3 run_lift.py                      # OEP closure, 3000 functions
    py -3 run_lift.py --max 20000
    py -3 run_lift.py --roots 0x00AD2451,0x004A1C30
    py -3 run_lift.py --virtual --max 60000   # plus every vtable method
    py -3 run_lift.py --all
"""
import argparse
from collections import defaultdict
import json
import os
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
# PCRECOMP picks another toolkit checkout, the same knob CMakeLists.txt has:
# the lifter and the runtime header must come from the same tree.
_TOOLS = os.path.join(os.environ.get('PCRECOMP', os.path.join(_HERE, '..', 'pcrecomp-rol')), 'tools')
sys.path.insert(0, os.path.join(_TOOLS, 'lift'))
sys.path.insert(0, os.path.join(_TOOLS, 'pe'))

from capstone import Cs, CS_ARCH_X86, CS_MODE_32          # noqa: E402
from generate import (EXTENT_REACH, alias_stub, closure, find_splits, true_extent,  # noqa: E402
                      linear_disassemble_function, lift_function_linear, write_chunk)
from lift32 import Lifter                                  # noqa: E402
from pe_analyze import analyze_pe, build_iat_map           # noqa: E402

EXE = os.path.join(_HERE, '_work', 'game', 'legends.exe')
CATALOG = os.path.join(_HERE, '_work', 'relift', 'functions.json')
SEEDS = os.path.join(_HERE, '_work', 'relift', 'rtti_seeds.json')
OUT = os.path.join(_HERE, 'src', 'recomp', 'gen')
STATS = os.path.join(_HERE, '_work', 'relift', 'lift_stats.json')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default=EXE)
    ap.add_argument('--catalog', default=CATALOG)
    ap.add_argument('--out', default=OUT)
    ap.add_argument('--roots', default='', help='comma-separated extra root VAs')
    ap.add_argument('--max', type=int, default=3000, help='closure size cap')
    ap.add_argument('--all', action='store_true', help='lift every function')
    ap.add_argument('--virtual', action='store_true',
                    help='root the closure at every RTTI vtable method too: most calls '
                         'in this binary are virtual, and the OEP alone reaches 8,053 functions')
    ap.add_argument('--split', type=int, default=400, help='functions per .c file')
    args = ap.parse_args()
    if not os.path.exists(args.catalog):
        sys.exit('no catalog at %s -- run disasm32.py first (README, Step by step)' % args.catalog)

    info = analyze_pe(args.exe)
    iat = build_iat_map(info)
    cs, ce = info.code_start, info.code_end
    print('[*] base=0x%08X code=0x%08X-0x%08X IAT=%d' % (info.image_base, cs, ce, len(iat)))

    cat = json.load(open(args.catalog))
    byaddr = {f['address']: f for f in cat['functions'] if cs <= f['address'] < ce}
    print('[*] catalog: %d functions inside .text' % len(byaddr))

    # Vtable slots. Bound each by the next known entry: a slot landing mid-code
    # handed `ce` makes the extent walk descend the whole of .text.
    want = set()
    if os.path.exists(SEEDS):
        want = {e['address'] for e in json.load(open(SEEDS))
                if cs <= e['address'] < ce and e['address'] not in byaddr}
    known = sorted(set(byaddr) | want)
    nxt = {a: (known[i + 1] if i + 1 < len(known) else ce) for i, a in enumerate(known)}
    for a in want:
        byaddr[a] = {'address': a, 'end': min(nxt[a], a + 0x100), 'calls_to': [],
                     'entry_kind': 'start'}
    print('[*] vtable slots not in the catalog: %d injected' % len(want))

    entry = info.image_base + info.entry_point_rva
    roots = [entry] + [int(x, 0) for x in args.roots.split(',') if x.strip()]
    if args.virtual and os.path.exists(SEEDS):
        roots += sorted(e['address'] for e in json.load(open(SEEDS)))
    if args.all:
        chosen = set(byaddr)
    else:
        chosen = set(closure(byaddr, roots, args.max))
    print('[*] lifting %d of %d functions (%s)'
          % (len(chosen), len(byaddr), 'all' if args.all else 'closure, cap %d' % args.max))

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    text = [s for s in info.sections if s.name == '.text'][0]
    code = open(args.exe, 'rb').read()[text.raw_offset:text.raw_offset + text.raw_size]

    # Known targets become RECOMP_CALL; anything else a decoded `call` names
    # becomes RECOMP_ICALL, which reports at run time instead of failing the build.
    # precise carry: adc/sbb take their carry from the flag state that set it.
    # The default reads a `_cf` that add/sub/cmp never write, which breaks
    # every 64-bit add and subtract -- and DirectShow's REFERENCE_TIME is
    # nothing but. The menu video's renderer judged all but 4 frames in 100 s
    # late and dropped them: the black main-menu backdrop (docs/bringup.md).
    # Off by default in lift32 only because Fury3 leans on the imprecision.
    lifter = Lifter(iat_map=iat, lifted=set(byaddr), precise_carry=True, precise_sbb=True)
    os.makedirs(args.out, exist_ok=True)
    for fn in os.listdir(args.out):                 # a smaller lift must not leave stale chunks
        if fn.startswith('recomp_') and fn.endswith('.c'):
            os.remove(os.path.join(args.out, fn))
    entries, chunk, idx, errors, dirty = [], [], 0, 0, 0
    ordered = sorted(byaddr)
    starts = {a for a in ordered if byaddr[a].get('entry_kind') != 'alias'}
    t_split = time.time()
    splits = find_splits(md, code, cs, ce, starts)
    starts -= splits          # still dispatchable; just not a wall inside its parent
    print('[*] split entries (the middle of the function before them): %d, %.0fs'
          % (len(splits), time.time() - t_split))
    t0 = time.time()

    def flush(force=False):
        nonlocal chunk, idx
        if chunk and (force or len(chunk) >= args.split):
            write_chunk(args.out, idx, chunk)
            idx += 1
            chunk = []

    def lift_one(addr, name, end, reached, aliases=None):
        nonlocal errors

        try:
            lo = min(reached) if reached else addr   # a chunk can sit below the entry
            insns, leaders = (linear_disassemble_function(md, code, cs, lo, end, reached=reached)
                              if end > addr else ([], None))
            if leaders is not None:
                leaders.add(addr)
            body = (lift_function_linear(lifter, name, insns, leaders, addr, alias_entries=aliases) if insns
                    else 'void %s(void) { }\n' % name)
        except Exception as e:                      # noqa: BLE001 -- counted, not hidden
            body = '/* ERROR %s: %s */\nvoid %s(void) { }\n' % (name, e, name)
            errors += 1
        chunk.append((body, addr, name))
        entries.append((addr, name))
        if len(chunk) >= args.split:
            flush()
            print('[*]   %d/%d (%d err)' % (len(entries), len(chosen), errors), flush=True)

    # A direct branch that leaves a body backward to an address nothing
    # catalogued is a tail call the catalog missed (__mtterm: jmp 0x00AD6FB7)
    # or a jump into shared code in a neighbour (hand-written x87 math:
    # __ffexpm1 jne 0x00AE01F0). A direct call can name one too: the catalog
    # dropped 0x00C10170, called directly, because a false start inside the
    # jump table before it decoded over it. Either way the target has to be dispatchable,
    # or the RECOMP_ITAIL cannot resolve. So each round's outside targets
    # become entries and are lifted in the next round, until none are new.
    def walk(addr):
        reached, behind, called = set(), set(), set()
        end, clean = true_extent(md, code, cs, addr, min(addr + EXTENT_REACH, ce), starts,
                                 reached=reached, behind=behind, called=called)
        return end, clean, reached, {t for t in behind if t not in reached} | called

    # An entry whose code another body already lifts (an alias, a split) is
    # entered there through a stub (generate.alias_stub), not lifted again:
    # as copies, aliases were 75% of the C and one 40 KB function became
    # 202 MB, more than a builder's memory. So walk the real starts first and
    # record which body owns each instruction.
    first = sorted(a for a in chosen if a in starts)
    walked, owner = {}, {}
    for addr in first:
        walked[addr] = walk(addr)
        for va in walked[addr][2]:
            owner.setdefault(va, addr)
    # A start can be inside another body too: the walk before it fell through
    # into it, which true_extent reads as a false start in that body (the 40 KB
    # function at 0x008BC530 held five, each re-lifting ~18k instructions). A
    # stub only ever enters a body that is lifted itself, never another stub.
    def host(a):
        o = owner.get(a)
        return o if o is not None and o != a and owner.get(o) == o else None
    inside = defaultdict(list)            # host body -> entries it carries
    for addr in sorted(chosen):
        if host(addr) is not None:
            inside[host(addr)].append(addr)
    # A vtable slot can name a method the catalog decoded over: a jump table
    # right before it read as code from a false start, and that decode runs
    # across the real start without stopping on it (0x00C805E0, reached only
    # from 0x00FB84D0's virtual call; five such in this binary, IDA agrees on
    # all five). The tell: no lifted body has an instruction starting there.
    # A vtable slot sits next to another slot naming a known function.
    # ponytail: neighbour test, not a full pointer-run scan. Upstream fix
    # belongs in disasm32.
    raw = open(args.exe, 'rb').read()
    over = set()
    for s in info.sections:
        if s.name == '.text':
            continue
        b = raw[s.raw_offset:s.raw_offset + s.raw_size]
        w = [int.from_bytes(b[o:o + 4], 'little') for o in range(0, len(b) - 3, 4)]
        for i, v in enumerate(w):
            if (cs <= v < ce and v not in byaddr and v not in owner and code[v - cs] not in (0xCC, 0x00)
                    and ((i and w[i - 1] in byaddr) or (i + 1 < len(w) and w[i + 1] in byaddr))):
                over.add(v)
    print('[*] data pointers to code no body starts an instruction at: %d' % len(over))
    stubbed = 0
    todo, added, outside = sorted(chosen), 0, set(over)
    while todo:
        for addr in todo:
            name = 'sub_%08X' % addr
            if host(addr) is not None:
                chunk.append((alias_stub(name, addr, 'sub_%08X' % host(addr)), addr, name))
                walked.pop(addr, None)
                entries.append((addr, name))
                stubbed += 1
                flush()
                continue
            end, clean, reached, out = walked.pop(addr, None) or walk(addr)
            outside |= out
            dirty += not clean
            lift_one(addr, name, end, reached, inside.get(addr))
        todo = sorted(t for t in outside if cs <= t < ce and t not in byaddr)
        outside = set()
        for t in todo:
            byaddr[t] = {'address': t, 'end': ce, 'calls_to': [], 'entry_kind': 'start'}
            chosen.add(t)
        added += len(todo)
    print('[*] branch and call targets outside the catalog, added as entries: %d' % added)
    print('[*] entries lifted as a stub into the body that owns their code: %d' % stubbed)

    stubs = [a for a in ordered if a not in chosen]
    for a in stubs:
        name = 'sub_%08X' % a
        chunk.append(('void %s(void) { RECOMP_NOT_LIFTED(0x%08Xu); }\n' % (name, a), a, name))
        entries.append((a, name))
        flush()
    flush(force=True)

    with open(os.path.join(args.out, 'recomp_funcs.h'), 'w', newline='\n') as f:
        f.write('/* Rise of Legends - AUTO-GENERATED by run_lift.py */\n#pragma once\n'
                '#include <stdint.h>\n\n'
                'void recomp_not_lifted(uint32_t va);\n'
                '#define RECOMP_NOT_LIFTED(va) recomp_not_lifted(va)\n\n')
        for a, n in entries:
            f.write('void %s(void);\n' % n)
    with open(os.path.join(args.out, 'recomp_dispatch.c'), 'w', newline='\n') as f:
        f.write('/* Rise of Legends - AUTO-GENERATED by run_lift.py */\n'
                '#include "recomp_types.h"\n#include "recomp_funcs.h"\n\n'
                'const recomp_dispatch_entry_t recomp_dispatch_table[] = {\n')
        for a, n in sorted(entries):
            f.write('    { 0x%08Xu, %s },\n' % (a, n))
        f.write('};\nconst uint32_t recomp_dispatch_count = %d;\n'
                'const uint32_t rol_entry_va = 0x%08Xu;\n' % (len(entries), entry))

    lines = sum(sum(1 for _ in open(os.path.join(args.out, fn), encoding='utf-8', errors='replace'))
                for fn in os.listdir(args.out))
    stats = {'lifted': len(chosen), 'stubs': len(stubs), 'alias_stubs': stubbed, 'errors': errors,
             'no_terminator': dirty, 'files': idx, 'lines': lines}
    json.dump(stats, open(STATS, 'w'), indent=1)
    print('=' * 60)
    print('  lifted %d   not-lifted stubs %d   errors %d   no terminator %d'
          % (len(chosen), len(stubs), errors, dirty))
    print('  %s lines of C in %d files, %.1fs' % (format(lines, ','), idx, time.time() - t0))
    print('=' * 60)


if __name__ == '__main__':
    main()
