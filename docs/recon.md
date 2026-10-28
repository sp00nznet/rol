# Recon: the binary, the engine, the disc

What Rise of Legends is made of, measured from the files. Where the target
executable comes from is in [install.md](install.md); the per-file numbers are
in `config/pe_*.json` (`pe_analyze.py` output).

## The target

**`legends.exe` v2.5**, the final official build. It is an ordinary PE:
Microsoft dropped the copy protection in the last patch, so there is nothing to
defeat and nothing to reconstruct.

| Property | v1.0 retail (on disc) | **v2.5, the target** |
|----------|----------------------|----------------------|
| Size | 10,143,000 bytes | 15,548,416 bytes |
| md5 | `40ff9fd21e13c878f41ba1d06688b46b` | `b0ebd5c3154ffd6c0e779d77183000a0` |
| Format | PE32, i386, Windows GUI | same |
| Image base | `0x00400000` | `0x00400000` |
| Entry point RVA | `0x010DB000`, inside `.idata` | `0x00C66ED2`, inside `.text` |
| Linker | 7.10, Visual C++ .NET 2003 | same |
| Build timestamp | 2006-04-16 | 2007-04-10 |
| Imports | 21 functions / 21 DLLs | **429 functions / 21 DLLs** |
| Sections | 6, three of them raw-size 0 | **5, all with real data** |
| `.text` | VSize 12.9 MB, **raw size 0** | 13.25 MB, entropy 6.27 |
| `.rdata` | 1.7 MB, entropy 7.997 | 1.7 MB, entropy 6.97 |
| `.data` | VSize 2.35 MB, **raw size 0** | 2.37 MB (164 KB initialized) |
| `.rsrc` | 400 KB | 400 KB |
| Trailing section | `.idata`: 8.0 MB, **executable**, entropy 7.92, holds the entry point | **none** |
| Verdict | SecuROM-class wrapper | **No protection or packer indicators** |

The v1.0 column is what we expected to fight. Code and data sections with a raw
size of zero are not in the file at all; one import per DLL is a protector's
stub IAT; an 8 MB high-entropy *executable* `.idata` holding the entry point is
the wrapper itself. The v2.5 column disassembles directly.

The PDB path `C:\rts2\Main\game\legends.pdb` names the engine: **rts2**, the
second-generation Big Huge Games RTS engine, successor to Rise of Nations'.

## What the code is

13.25 MB of x86 in `.text`, about six times a 2000-era title like Crimson
Skies, and the largest binary pcrecomp has been pointed at. C++ throughout,
MSVC 7.1, `__thiscall` heavy, namespaced under `BHG::`. RTTI exists only for
the CRT and iostreams (21 `.?AV` descriptors): the game's own classes were
built without it, so vtables have to come from `.rdata` pointer runs rather
than type descriptors.

The binary carries its own diagnostics: assert and log strings naming real
methods (`BHG::SoundManager::init`), some as full `__thiscall` prototypes.
`tools/symbols.py` turns them into a partial symbol map ([catalog.md](catalog.md#symbols)).

## Imports (427 across 21 DLLs in the unwrapped v1.0; 429 in v2.5)

| DLL | Fns | What it says |
|-----|-----|--------------|
| kernel32 | 176 | files, threads, memory, `LoadLibraryA` |
| user32 | 67 | window, message pump, input |
| gdi32 | 30 | fonts, device contexts |
| ws2_32 | 28 | multiplayer |
| advapi32 | 21 | registry + CryptoAPI (key exchange for online play) |
| winmm | 20 | timers, joystick |
| imm32 | 15 | IME, full CJK text input |
| binkw32 | 13 | RAD Bink video |
| avifil32 | 11 | AVI capture (the in-game recorder) |
| winhttp | 10 | the built-in updater |
| oleaut32 / ole32 / rpcrt4 | 19 | COM plumbing |
| pdh | 6 | performance counters: the engine profiles itself |
| version / psapi / shell32 / shfolder | 8 | install paths, memory stats |
| ddraw | 1 | `DirectDrawCreateEx`, adapter enumeration only |
| dsound | 1 | ordinal 11, `DirectSoundCreate8` |
| wmvcore | 1 | Windows Media, the `.wma` voice-over bank |

There is **no `d3d9.dll` import**. Direct3D is loaded with `LoadLibraryA` at
run time, so the graphics layer already sits behind a late-bound seam.

## Middleware on the disc

| Component | File | Note |
|-----------|------|------|
| NovodeX PhysX | `NxPhysics.dll`, `NxCooking.dll` | pre-Ageia PhysX 2.x; 32-bit, stays real |
| RAD Bink | `binkw32.dll` | cut-scene playback, stays real |
| SpeedTree RT | statically linked (`CSpeedTreeRT` symbols in `.text`) | lifted with the rest |
| D3DX stub | `d3d8xstub.dll` | shipped shim over D3DX |
| Fluid sim | `fluidModel.dll` | Big Huge Games' own |
| Script compiler | `script_compiler.exe` | the game ships its own script toolchain |
| Patch tooling | `patch.exe`, `patcher.dll`, `patchw32.dll`, `updater.exe` | RTPatch 8.10 |

## Data formats (not read yet)

`BIGS/*.big` archives (the binary's own `BigFile` class), XML rules and UI
layout (`BXMLElement*`), `loc/<LANG>/` subtitles and fonts, `.bik` video,
`.wma` voice-over, and a `StringLookup` binary string table built from XML at
load time.

## Provenance

A recompilation is only as trustworthy as the bytes it starts from, and the
widely circulated "ISO ALL-IN-ONE" EN set carries a scene crack beside the
retail files. So the target was checked against a disc nobody had touched.

| Source | Standing | Note |
|--------|----------|------|
| **EN 4-CD set, from a physical collection** | **Clean** | Alcohol 120% dump of real discs (2022), 1200 dpi media scans, no crack directory. Read errors only in sectors 1034-1043, the protection's deliberately unreadable block |
| Taiwan release, 4 discs | Verified | redump.org discs 81912-81915, but the Traditional Chinese build |
| EN "ISO ALL-IN-ONE" | Tampered | Carries a scene crack directory |
| 2.5 patch | n/a | The circulating copy is an unofficial German Inno Setup wrapper around Microsoft's RTPatch deltas |

Every shipped binary, clean dump against the circulated set:

```
MATCH   legends.exe          40ff9fd21e13c878f41ba1d06688b46b
MATCH   binkw32.dll  NxPhysics.dll  NxCooking.dll  d3d8xstub.dll
MATCH   fluidModel.dll  patcher.dll  patchw32.dll  script_compiler.exe  (+6 more)
DIFFER  mgspid.dll           81,920 bytes retail vs 57,344 bytes
```

One file differs: the Microsoft Games product-ID DLL, the key check, which this
project never calls. The executable is byte-identical, and the comparison is
its own control: it would have caught tampering, and found none.

The chain from a physical disc to the disassembled binary is: retail discs ->
MSI tables -> assembled tree -> Microsoft's patch -> `b0ebd5c3154ffd6c0e779d77183000a0`.
