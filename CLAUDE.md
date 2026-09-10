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

## Open Questions
- Discs 2-4 assets are now installed in `_work/game`; `.big` format not yet read.
- The other ten RTPatch deltas reject our disc build. Unknown whether a
  different retail pressing matches them. Irrelevant unless an intermediate
  build is ever needed — the final one applies.
- Symbol harvest yields only 182 distinct names. Are there richer name tables
  (profiler/telemetry) in `.rdata` that a pointer-run scan would find?
