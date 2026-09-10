# Rise of Legends — Static Recompilation

A static recompilation of **Rise of Nations: Rise of Legends** (Big Huge Games / Microsoft Game Studios, 2006), targeting modern Windows with native x86 execution.

This is a preservation project. Rise of Legends is the Big Huge Games RTS you **cannot buy anywhere** — no Steam, no GOG, no Microsoft Store, no re-release. Its predecessor *Rise of Nations* got an Extended Edition on Steam in 2014; *Rise of Legends* got nothing. The retail discs are wrapped in SecuROM-class protection, the game shipped a DirectX 9 renderer that argues with modern drivers, and its online component is long dead. If it is going to survive, someone has to take it apart.

**Bring your own disc. No game files, no game binaries and no extracted assets are committed here — ever.**

## Project Status

| Phase | Status | Description |
|-------|--------|-------------|
| **Phase 0** | **Complete** | Recon — disc layout, PE analysis, DRM identification, engine fingerprinting |
| **Phase 1** | In Progress | Unwrap — produce a clean, import-rebuilt image from a retail install |
| **Phase 2** | In Progress | Function discovery — recursive descent over 12.9 MB of x86 |
| **Phase 3** | **Started** | Symbol recovery — `tools/symbols.py` finds 182 distinct method names at 488 call sites |
| Phase 4 | Pending | Lifting — x86-32 → C (`lift32_cpu.py`, CPU-struct model, hybrid boundary) |
| Phase 5 | Pending | Build & link |
| Phase 6 | Pending | Runtime bringup — CRT init, static constructors, `WinMain` |
| Phase 7 | Pending | Platform layer — Win32, D3D9, DirectSound; middleware DLLs stay real |
| Phase 8 | Pending | Asset layer — `.big` archives, XML rules, localisation |
| Phase 9 | Pending | Game loop — simulation, terrain, render |
| Phase 10 | Pending | Modernisation — widescreen, high DPI, modern input, LAN/online revival |

## Binary Analysis

The retail executable is protected. Below: `legends.exe` v1.0 as it ships on disc 1, beside a clean unwrapped image of the same build.

| Property | Retail (on disc) | Unwrapped image |
|----------|------------------|-----------------|
| Size | 10,143,000 bytes | 25,731,072 bytes |
| Format | PE32, i386, Windows GUI | same |
| Image base | `0x00400000` | `0x00400000` |
| Entry point RVA | `0x010DB000` (inside `.idata`) | `0x00C581A2` (inside `.text`) |
| Linker | 7.10 — Visual C++ .NET 2003 | same |
| Build timestamp | 2006-04-16 | 2006-03-29 |
| Imports | 21 functions / 21 DLLs | 427 functions / 21 DLLs |
| `.text` | VSize 12.9 MB, **raw size 0** | VSize 12.9 MB, raw 12.9 MB |
| `.rdata` | 1.7 MB, entropy 7.997 | 1.7 MB |
| `.data` | VSize 2.35 MB, **raw size 0** | 2.35 MB |
| `.rsrc` | 400 KB | 400 KB |
| Trailing section | `.idata` — 8.0 MB, **executable**, entropy 7.92, holds the entry point | inert |
| PDB path | — | `C:\rts2\Main\game\legends.pdb` |

Read the retail column as a diagnosis. Code and data sections with a raw size of zero are not in the file at all; one import per DLL is a protector's stub IAT; an 8 MB high-entropy *executable* `.idata` holding the entry point is the wrapper itself. Nothing can be disassembled until the image is whole, which is why Phase 1 is mandatory rather than optional.

The build path `C:\rts2\Main\game\` names the engine: **rts2**, the second-generation Big Huge Games RTS engine, successor to the one behind Rise of Nations.

### What the code actually is

12.9 MB of x86 in `.text` — roughly six times the code volume of a 2000-era title like Crimson Skies. Expect tens of thousands of functions. This is the largest target attempted in this family so far, and the roadmap is written accordingly: the lifting is automated, the bring-up is not.

C++ throughout, MSVC 7.1, `__thiscall` heavy, namespaced under `BHG::`. RTTI is present only for the CRT and iostreams — the game's own classes were built without it, so vtable recovery has to come from data-section pointer scanning rather than type descriptors.

### Imports (unwrapped image, 427 across 21 DLLs)

| DLL | Fns | What it says |
|-----|-----|--------------|
| kernel32 | 176 | files, threads, memory, `LoadLibraryA` |
| user32 | 67 | window, message pump, input |
| gdi32 | 30 | fonts, device contexts |
| ws2_32 | 28 | multiplayer |
| advapi32 | 21 | registry + CryptoAPI (key exchange for online play) |
| winmm | 20 | timers, joystick |
| imm32 | 15 | IME — full CJK text input |
| binkw32 | 13 | RAD Bink video |
| avifil32 | 11 | AVI capture (the in-game recorder) |
| winhttp | 10 | the built-in updater |
| oleaut32 / ole32 / rpcrt4 | 19 | COM plumbing |
| pdh | 6 | performance counters — the engine profiles itself |
| version / psapi / shell32 / shfolder | 8 | install paths, memory stats |
| ddraw | 1 | `DirectDrawCreateEx` — adapter enumeration only |
| dsound | 1 | ordinal 11 — `DirectSoundCreate8` |
| wmvcore | 1 | Windows Media — the `.wma` voice-over bank |

There is **no d3d9.dll import**. Direct3D is loaded dynamically at runtime, which is good news: the graphics layer already sits behind a late-bound seam we can intercept.

### Middleware on the disc

| Component | File | Note |
|-----------|------|------|
| NovodeX PhysX | `NxPhysics.dll`, `NxCooking.dll` | pre-Ageia PhysX 2.x; 32-bit, stays real |
| RAD Bink | `binkw32.dll` | cut-scene playback, stays real |
| SpeedTree RT | statically linked (`CSpeedTreeRT` symbols in `.text`) | lifted with the rest |
| D3DX stub | `d3d8xstub.dll` | shipped shim over D3DX |
| Fluid sim | `fluidModel.dll` | Big Huge Games' own |
| Script compiler | `script_compiler.exe` | the game ships its own script toolchain |
| Patch tooling | `patch.exe`, `patcher.dll`, `patchw32.dll`, `updater.exe` | RTPatch; latest official build is 2.5 |

### Data formats to reverse

`BIGS/*.big` archives (the binary's own `BigFile` class), XML rules and UI layout (`BXMLElement*`), `loc/<LANG>/` subtitles and fonts, `.bik` video, `.wma` voice-over, and a `StringLookup` binary string table built from XML at load time.

## Roadmap

### Phase 1 — Unwrap

The retail image is incomplete on disc; a whole one has to come from a running process, dumped and re-linked with a rebuilt import table.

**The patch chain, investigated.** The 2.5 patch installer is Inno Setup 5.5 wrapping eleven RTPatch deltas that step the install from build `0604.2001.0000` (retail, 20 April 2006) to `0704.1001.0000` (10 April 2007). There is no loose executable in it — `legends.exe` arrives as a compressed payload of roughly 9.8 MB inside the last delta. That size is ambiguous on its own: it fits both a wrapped 10 MB executable copied wholesale *and* an unwrapped 25 MB one squeezed down. Settling it means installing the game, applying the chain with the shipped `patch.exe` / `patchw32.dll`, and re-running section analysis on the result. Worth the half hour, because a patched build that dropped the wrapper would delete this phase and hand us the final balance patch at the same time.

**The dump route**, if the patch does not settle it: same shape as the SafeDisc dumper already in pcrecomp — attach, let the wrapper finish unpacking, snapshot the image, walk the IAT and rebuild imports by name.

Output: `legends_unwrapped.exe` — produced locally by whoever owns the disc, never distributed.

### Phase 2 — Function discovery

Recursive descent from the entry point, plus data-section scans for `push imm32` function pointers and vtable arrays. With no game RTTI, vtable recovery leans on `.rdata` pointer-run detection.

### Phase 3 — Symbol recovery *(the lever unique to this title)*

The binary carries its own diagnostics: assert and log strings that spell out real method names — `BHG::SoundManager::init`, `BHG::D3DVertStream::lock` — some as full prototypes with parameter types. `tools/symbols.py` pulls the scoped names out of those strings and scans every section for 4-byte references to them:

```
   174 strings naming a scoped symbol (182 distinct names)
   172 of them referenced by code (488 sites)
```

Those 488 sites become named functions as soon as Phase 2 hands over function boundaries — a partial symbol table out of a stripped binary, for four seconds of scanning. Names are only where the engine happened to assert, so this is a seed, not a map; but a seed that includes the renderer's vertex-stream lock is worth a week of tracing.

### Phase 4-6 — Lift, build, boot

Standard pcrecomp pipeline: `lift32_cpu.py` to a reentrant CPU-struct C model, hybrid boundary so the real MSVC 7.1 CRT keeps running while the game body is recompiled, then CRT init → static constructors → `WinMain`.

### Phase 7 — Platform layer

Win32 through the existing compat layer. Direct3D is late-bound, so the seam is already there. DirectSound, Bink, PhysX and the D3DX stub are 32-bit DLLs sitting on the disc — they stay real and get called, not reimplemented.

### Phase 8-10 — Assets, gameplay, modernisation

`.big` mounting and XML rules first (nothing renders without them), then the simulation and render loop, then the things the original never had: proper widescreen, high DPI, modern input, and a multiplayer stack that does not depend on a service shut down years ago.

## Provenance

A recompilation is only as trustworthy as the bytes it starts from, and the widely circulating "ISO ALL-IN-ONE" EN set carries a scene crack alongside the retail files. That crack's unwrapped executable is useful as a *structural* reference — it proves what a correct unwrap should look like — but it is not something to build a pipeline on, and it is not evidence of what the genuine disc contains.

So the target binary was checked against a disc nobody had touched.

| Source | Standing | Note |
|--------|----------|------|
| **EN 4-CD set, from a physical collection** | **Clean** | Alcohol 120% dump of real discs (2022), 1200 dpi scans of media and manual, no crack directory. Read errors confined to sectors 1034-1043 — the protection's deliberately unreadable block, not game data |
| Taiwan release, 4 discs | Verified | redump.org discs 81912-81915, but the Traditional Chinese build |
| EN "ISO ALL-IN-ONE" | Tampered | Carries a scene crack directory; this is the widely circulated one |
| Official 2.5 patch | n/a | The circulating copy is an unofficial German Inno wrapper around the real RTPatch deltas |

Comparing every shipped binary across the clean dump and the circulated one:

```
MATCH   legends.exe          40ff9fd21e13c878f41ba1d06688b46b
MATCH   binkw32.dll  NxPhysics.dll  NxCooking.dll  d3d8xstub.dll
MATCH   fluidModel.dll  patcher.dll  patchw32.dll  script_compiler.exe  (+6 more)
DIFFER  mgspid.dll           81,920 bytes retail vs 57,344 bytes
```

One file differs, and it is the Microsoft Games product-ID DLL — the key check, which this project never calls. **`legends.exe` on the circulated image is byte-identical to the one on a physically dumped retail disc**, so every number in this README describes the genuine retail binary. That comparison is also its own control: it would have caught tampering in the executable, and it didn't.

## Repository Layout

```
config/         Phase 0 analysis output (PE structure, import tables, disc catalog)
docs/           Design notes and per-phase write-ups
tools/
  symbols.py    Harvest the binary's own method-name strings and their call sites
                (`--selftest` runs its checks; no game files needed)
```

Generic analysis and lifting tools live in [pcrecomp](https://github.com/sp00nznet/pcrecomp) and are not duplicated here.

## Legal

This repository contains analysis, tooling and original source code only. No game code, no game assets. You need your own retail copy of Rise of Legends for any of it to be useful. Rise of Nations: Rise of Legends is © 2006 Microsoft Corporation.
