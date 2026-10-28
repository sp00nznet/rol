# The function catalog, scored

How pcrecomp's `disasm32` does on the largest binary it has seen, measured
against IDA Pro 9.1 (headless idalib) with pcrecomp's `score_recovery.py`. IDA
is a strong second opinion here, not ground truth: there is no source or PDB
for Rise of Legends. Scores are in `config/recovery_score*.json`.

## Current (2026-10-02 relift, pcrecomp 86fe9f5)

| | Sep (legacy) | Sep (ownership fix) | **Oct relift** |
|---|---|---|---|
| precision | 44.9% | 56.3% | **69.9%** |
| recall | 99.27% | 99.09% | **99.53%** |
| missed functions | 237 | 296 | **154** |
| exact function ends | n/a | n/a | **87.9%** |

61,824 catalog entries, aliases held out of precision. Upstream's extent
clamping and interior checks beat this repo's own ownership-aware decoder on
every axis, so that decoder was retired (it is in git history at `e68116e`).

pcrecomp has moved 158 commits since 86fe9f5, several of them catalog fixes
(zero fill is not a function, code addresses pushed into holes, data kept in
code). These numbers predate them.

## The first catalog (2026-09-10)

```
[*] Data scan: 25513 functions reachable only via data pointers...
[*] Successfully disassembled 72246 functions (9 discovery rounds)
[*] Functions: 72246  (thunks=123, leaves=14212)
[*] Instructions: 19,171,481
[*] Byte coverage: 13,223,286 / 13,252,097 (99.8% of code range)
```

IDA analyzed the same binary in **70 seconds** (against 50 minutes) and found
32,662 functions:

| | |
|---|---|
| True positives | 32,425 |
| False positives | 39,821: **31,763 split**, 8,058 invented |
| False negatives | **237** |
| Precision | 44.88% |
| Recall | 99.27% |

A **split** is an address inside a function IDA knows: one function entered
twice. **Invented** is outside every known function: possibly data decoded as
code. Four of five false positives were splits, concentrated (one function took
238 "starts"; the worst 5% of affected functions held 37%). Trespasser saw the
same shape on a 1998 binary, so it is a property of the tool.

## What the splits cost, and what causes them

Three theories, two wrong:

- **Wrong: the splits are spurious and should be deleted.** They are
  load-bearing. `disasm32` promotes a jump into another function's body to an
  entry so the lifter can tail-dispatch to it; EH funclets and switch arms must
  stay dispatchable.
- **Wrong: unrecognized jump tables cause them.** IDA recovered 770 switches
  with 6,341 arm targets, at most 20% of the splits. (A capstone linear-sweep
  probe claiming 16 tables was unsound: it desynchronizes on data in code and
  covered 7.3% of `.text`.)
- **Right: duplication.** IDA counts 3,449,319 instruction heads in `.text`;
  the catalog decoded 19,171,481, **5.6x**. `disassemble_function` decoded from
  its start with no knowledge of what other functions owned, so an entry
  landing mid-function re-decoded the whole remainder.

Measure duplication by instructions decoded, never by summed `size` fields:
`size` is `end - address`, and `end` inflates when descent follows a far jump
(mean 10,542 bytes against a median of 184). That route gives a bogus 57.6x.

## The ownership fix (retired, kept for the record)

Threading the `owner` map into the decoder, so foreign-owned code stops a block
and becomes a tail transfer, took duplication from 5.56x to 1.00x
(3,527,728 instructions) and precision to 56.28%, at the cost of 1,107 bytes of
real code no longer covered and recall 99.27% -> 99.09% (59 functions legacy
had found by accident, by decoding garbage that happened to contain a `call`).
Upstream later solved the same problem differently and better (table above).

Two instrument failures from that work: `score_recovery.py` rebuilt a
57,507-element set once per reference function and died with `MemoryError`
under load (fixed upstream); and a `CS_ERR_MEM` crash in capstone turned out to
be six other projects' disassemblers running at once. Re-running on an idle
machine completed identically.

## Symbols

`tools/symbols.py --functions` binds the binary's own diagnostic strings to the
functions that reference them:

```
   174 strings naming a scoped symbol (182 distinct names)
   172 of them referenced by code (453 sites)
   297 functions reference a scoped name (273 of them exactly one)
```

That names **143 functions** along the startup path (`Game::init`,
`Game::setup_build_cities`, `GameOut::init_terrain_render`). The engine
announces each startup phase by name, so the names label exactly the path a
bring-up walks first. The map is derived from the binary, so it lands in
`_work/` and is never committed.
