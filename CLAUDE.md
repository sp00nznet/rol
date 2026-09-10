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

## Open Questions
- Does official patch 2.5 ship a thinner `legends.exe`? Test before writing a
  dumper — it could delete Phase 1 outright.
- Discs 2-4 hold the bulk of `.big` assets; not yet catalogued.
