# Rise of Legends Static Recompilation — Project Memory

## Standing Directive
Keep README.md current with real numbers as phases land. Commit and push to
`main` on `sp00nznet/rol` (private) regularly. Never commit game binaries or
assets — `.gitignore` blocks `*.exe`, `*.dll`, `*.big`, `_work/`.

## Target
Rise of Nations: Rise of Legends (Big Huge Games / Microsoft, 2006).
Engine: **rts2** (PDB path `C:\rts2\Main\game\legends.pdb`), namespace `BHG::`,
MSVC 7.1 / linker 7.10, PE32 x86, image base 0x400000.

## Key Paths
- Repo root: `G:/recomp/pc/rol`
- Working scratch (gitignored): `G:/recomp/pc/rol/_work`
  - `_work/cd1.iso` — disc 1 converted from MODE1/2352 bin (2352→2048 stride)
  - `_work/_iso/program files/Microsoft Games/Rise Of Legends/` — retail files
    straight off disc 1; the installer only copies, so no MSI extraction needed
  - `_work/_iso/DEViANCE/legends.exe` — unwrapped reference image found on the
    disc image. Structural reference only; the pipeline must target an image the
    disc owner produces themselves (Phase 1)
  - `_work/cd1_retail.iso` + `_work/_retail/` — disc 1 from a clean physical
    Alcohol 120% dump (archive.org `rise-of-nations-rise-of-legends-2006-
    microsoft-big-huge-games-4-cd-set`, 7z md5 3c872c2a169a7c4733fd03cc47a69ee1).
    MDF is **2448 bytes/sector** (RAW+SUB-96): take bytes 16..2064 of each block.
    The scene disc 1 bin is MODE1/2352 — different stride, don't mix them up.
- Source discs: `G:/recomp/pc/rol/*.zip` (4 CDs + unofficial 2.5 patch installer)
- Toolbox: `G:/recomp/pc/tools` = the `pcrecomp` repo. Do not duplicate its tools.

## Phase 0 Facts (established 2026-09-09)
- Retail `legends.exe`: 10,143,000 bytes, entry RVA `0x010DB000` inside an 8 MB
  executable high-entropy `.idata`; `.text`/`.data`/`.tls` have **raw size 0**;
  21 imports across 21 DLLs. SecuROM-class wrapper — unwrap before anything else.
- Unwrapped image: 25,731,072 bytes, entry RVA `0x00C581A2`, 427 imports,
  `.text` VSize 0x00C95000 = **12.9 MB of x86 code**.
- No `d3d9.dll` import — D3D is late-bound via `LoadLibraryA`. That seam is the
  cheapest place to put a modern renderer later.
- RTTI exists only for CRT/iostream types (21 `.?AV` descriptors). Game classes
  have none — vtable recovery needs `.rdata` pointer-run scanning.
- The binary embeds its own diagnostic strings naming real methods
  (`BHG::SoundManager::init`, `BHG::D3DVertStream::lock`, some as full
  `__thiscall` prototypes). Phase 3 turns those into a symbol table.
- Middleware: NovodeX PhysX 2.x, RAD Bink, SpeedTree RT (statically linked),
  `d3d8xstub.dll`, `fluidModel.dll`. All 32-bit DLLs on disc — keep them real.

## Useful Commands
```bash
# raw CD bin (MODE1/2352) -> iso, streamed straight out of the zip
python -c "...read 2352-byte sectors, keep bytes 16..2064..."   # see git log

# PE analysis (note: --json takes a PATH argument, not a flag)
python /g/recomp/pc/tools/tools/pe/pe_analyze.py legends.exe --json config/pe.json
python /g/recomp/pc/tools/tools/pe/analyze_sections.py legends.exe   # DRM/packing
python /g/recomp/pc/tools/tools/disasm/disasm32.py legends.exe -o functions.json
```

## Provenance (settled 2026-09-09)
Retail `legends.exe` md5 **40ff9fd21e13c878f41ba1d06688b46b**, 10,143,000 bytes.
Byte-identical between the circulated scene ISO and a clean physical disc dump.
Every shipped binary matches across the two except `mgspid.dll` (81,920 retail
vs 57,344 scene) — the Microsoft Games product-ID/key-check DLL, which this
project never calls. The scene release added a crack folder and swapped that one
DLL; it did not touch the game. No need to re-litigate this.

## THE TARGET (settled 2026-09-09)
`_work/game/legends.exe` — **v2.5, unprotected**, 15,548,416 bytes,
md5 `b0ebd5c3154ffd6c0e779d77183000a0`, built 2007-04-10, entry RVA
`0x00C66ED2` in `.text`, 5 clean sections, 429 imports, 13.25 MB of code.
Phase 1 (unwrap) is **cancelled** — Microsoft dropped the protection in the
final patch. Do not go back to the DEViANCE image; it is obsolete.

### How the install was built (repeatable)
1. Loose trees from disc 1 + disc 2 (`program files\Microsoft Games\Rise Of
   Legends`) copied to `_work/game`.
2. All four `DiskNC~1.cab` put in ONE folder (spanned entries need siblings
   present), extracted to `_work/cabstage` — entries are named by MSI File key.
3. `scratchpad/dump_msi.ps1` + `place.py` resolve File/Component/Directory
   tables to real names and paths (root dir id is `INSTALLDIR`). 2,586 placed,
   all matching the MSI's recorded FileSize. Result: 2.8 GB, 3,545 files,
   no registry, no CD key, nothing system-wide.
4. `patch.exe` → copy as `rtp.exe`; syntax `rtp.exe <dir> <patchfile.RTP>`.

### RTPatch gotchas
- Windows auto-elevates any exe named `patch.exe` (legacy installer detection,
  no manifest involved). Fix: `$env:__COMPAT_LAYER='RunAsInvoker'`. Renaming
  alone does NOT help — the version resource triggers it too.
- RTPatch 8.10 is **transactional**: stages to temp files, commits only on
  success. So a failed step changes nothing, and every step can be probed
  against an untouched tree safely.
- Only `RETAIL-0612.1201.0000-0.0704.1001.0000.RTP` applies to a v1.0 tree
  (51 files, full replacements, baseline-independent). The other ten abort with
  `ept0036` (old file content mismatch). That one is all we need.

## Patch 2.5 (investigated 2026-09-09)
`RoL_Patch2.5.exe` is Inno Setup 5.5 — local `innounp` is too old; use
`innoextract` 1.9 (`innoextract -e -m -d <dir> RoL_Patch2.5.exe`). Payload is
eleven RTPatch deltas chaining build `0604.2001.0000` (retail) →
`0704.1001.0000` (v2.5, Apr 2007), plus `patch_control.xml` listing the order.
No loose exe: `legends.exe` sits at offset ~147.6M in the last RTP with ~9.8 MB
of compressed payload before the next entry — ambiguous between a wholesale copy
of the 10 MB wrapped exe and a squeezed 25 MB unwrapped one. **Do not reverse
the RTP container** (that was a rabbit hole); install the game, run the chain
with the shipped `patch.exe`/`patchw32.dll`, then `analyze_sections.py` the
result. The archive.org copy of the patch is byte-identical in size to the local
one — same wrapper, nothing gained by downloading it.

## Phase 2 result (2026-09-10)
`_work/functions_v25.json` (22.9 MB): 72,246 entries, 19,171,481 instructions,
**99.8% byte coverage**, 9 discovery rounds (converged: last round 13 targets).
Runtime ~50 min.

**The 72,246 is inflated — quote coverage, not the count.** Measured from the
named subset: 273 named entries carry only 143 distinct names, and 113 of 130
duplicate-name address gaps are under 4 KB (one function split, not two).
7,584 entries are under 16 bytes. Don't multiply by the 1.9:1 ratio — the named
sample is startup code and only 273 entries. Measure properly instead: score
recovery against ground truth the way trespasser did (P 77.1% / R 78.5%) BEFORE
committing to a lift. See `G:/recomp/pc/trespasser` — same toolchain, and its
audit method is the model to copy.

## CHECK PCRECOMP'S LOG BEFORE LONG JOBS
The toolbox is shared and moves under us. Lost 2.5 h to this: a run started at
21:18 used pre-fix `disasm32.py`; commit `e9d96cb` ("make disassemble_at lazy,
~20x faster") hit disk at 21:47. Python reads source at startup, so a
mid-flight fix does nothing for a running job — restart it. Also: never pipe a
long job through `tail` (buffers everything, blinds you); redirect to a log
file and use `-u`.

## Splits: diagnosed 2026-09-10 (READ BEFORE "FIXING" THEM)
**Do not delete split entries.** They are load-bearing: `disasm32` deliberately
promotes a jump into another function's body to an entry point so the lifter
can tail-dispatch to it. EH funclets and switch arms must stay dispatchable.

Jump tables are NOT the main cause — IDA: 770 switches, 6,341 arm targets, at
most 20% of the 31,763 splits. (A capstone linear-sweep probe claiming "16
tables" was unsound: it desyncs on data-in-code, covered 7.3% of `.text`.)

**The real defect is duplication, not entry count.** IDA counts 3,449,319
instruction heads in `.text`; our catalog decoded 19,171,481 → **5.6x**.
`disassemble_function` (disasm32.py:241) decodes from its start with no
knowledge of `owner`, following jumps within ±0x100000, so an entry landing
mid-function re-decodes the whole remainder.

Do NOT quote a duplication factor from summed `size` fields — `size` is
`end - address` and `end` inflates when descent follows a far jump (mean 10,542
vs median 184 bytes). That path gives a bogus 57.6x. Use instructions decoded.

**The fix is built and measured**: `tools/disasm32_owned.py` (local working copy
of pcrecomp's disasm32, NOT yet upstreamed). Threads `owner` into
`disassemble_function`; foreign-owned code stops a block and gets a CFG edge.
Entries starting on foreign code become `alias_of` records (kept even with no
blocks — `_add_func` would otherwise drop them and kill the dispatch), and the
owner's block is split at the alias address to give the lifter a label.
`--legacy-duplicate` restores the old behaviour for comparison.

Results: duplication **5.56x -> 1.00x** (19,171,481 -> 3,527,728 instructions
vs IDA's 3,449,319 heads), entries 72,246 -> 57,507 (2,464 aliases, 1,993
blocks split), precision 44.88% -> 56.28%.

**Two small regressions, unfixed, do not lose track of these:**
- coverage -13,956 bytes, of which 12,849 is non-code legacy shouldn't have
  decoded (a win) but **1,107 bytes are real code inside IDA functions**.
- recall 99.27% -> 99.09% (237 -> 296 misses). Those 59 were found by accident:
  decoding garbage sometimes decodes a `call` to a real function.

**Tried and reverted:** letting an alias walk past its foreign start gave
byte-identical coverage (the scan stops at the next foreign instruction
anyway). Don't re-try it without a new argument.

**Still required before upstreaming:** `lift32` must emit the transfer for
`alias_of` entries (decoder half alone leaves them lifting to nothing), plus a
regression run on a second project's binary (crimsonskies or fury3 — both
lifted and running, so a change there is loud).

### FIELD-NAME MISMATCH TO RESOLVE FIRST (noticed 2026-09-13)
Another session (trespasser) extended `score_recovery.py` upstream while this
was in flight, and it reached the same conclusion independently: on Trespasser
**6,427 of 7,364 false positives were aliases — 87% of the reported error**.
Their version holds aliases out of precision entirely and scores function ends
too. But it keys on **`"entry_kind": "alias"`**, while `disasm32_owned.py`
emits **`"alias_of": <addr>`**. Converge on theirs (`entry_kind`, keeping
`alias_of` for the target address) before upstreaming, or the scorer silently
counts every one of our 2,464 aliases as a false positive.

Their scorer also warns: trim trailing `0xCC`/`0x90` from reference ends before
comparing, or correct functions read as truncated (82% of their "short" ends
were padding). Do NOT trim `0x00` — it is a legal encoding.

pcrecomp has also moved on: `321b633 "Follow the jump: a jmp inside a function
is not the end of it"` looks directly relevant to ownership/splits. **Re-read
pcrecomp's log and rebase this work on current HEAD before doing anything
else** — `disasm32_owned.py` was copied from an older revision.

## Open Questions
- Discs 2-4 assets are now installed in `_work/game`; `.big` format not yet read.
- The other ten RTPatch deltas reject our disc build. Unknown whether a
  different retail pressing matches them. Irrelevant unless an intermediate
  build is ever needed — the final one applies.
- Symbol harvest yields only 182 distinct names. Are there richer name tables
  (profiler/telemetry) in `.rdata` that a pointer-run scan would find?
