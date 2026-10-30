# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

## [0.1.0-alpha.1] - 2026-10-30

The first playable build: Quick Battle skirmishes and the Vinci campaign's
first mission run recompiled, with an F10 menu of enhancements, and an
installer that builds it from your own discs.

### Added
- **Enhancements and the F10 menu** (`src/runtime/enhance.c`,
  `docs/enhancements.md`): borderless fullscreen at any resolution with a
  render scale (1080p scaled 2x on a 4K screen, where the fixed-size interface
  would be tiny); 4x MSAA and 4096/8192 shadow maps, both the engine's own and
  never offered by its options screen; 16x anisotropic filtering; CAS
  sharpening and vivid colour as a post-process; zoom out 1.5x or 2x with the
  fog pushed back (`tools/make_enhanced_data.py` rewrites the camera and
  tileset archives into `rol_data\`); a frame-rate overlay and the game's own
  clock, score, health bars and gather rates; photo mode (hide the interface,
  F9 PNG screenshots). Settings in `rol.ini`.
- `tools/bigtool.py`: lists, extracts and rewrites the game's `.big` archives
  and their binary XML.
- **Installer**: `launcher/launcher.json` and `.github/workflows/installer.yml`
  for recomp-launcher; `tools/setup.ps1 -Yes -Game <folder>` runs unattended,
  builds, makes the zoom data and writes `Rise of Legends (recomp).cmd`.
- In game: Quick Battle loads and plays (the economy ticks, the AI plays); the
  campaign runs its cinematic and briefing into mission 1, whose first quest
  completes under scripted orders.
- Scripted input: `--rclick`, `--drag`, `--wheel`; extended keys (Page Up, the
  arrows) reach DirectInput and the window as themselves, not the keypad.
- Host: answers `GetSystemMetrics(SM_REMOTESESSION)` as local (the game
  refuses remote sessions); reports first-chance faults only in its own image
  (dxdiagn.dll's own handled fault had killed the game); records antialiased
  frames through a resolve.
- `run_lift.py`: data pointers to methods the catalog decoded over (a jump
  table before them read as code) become entries; Quick Battle called one.

### Fixed
- Start Game overflowed the host stack: a loop split by a chain of false
  entries ran as one nested call per iteration (pcrecomp #155).

### Added during bring-up
- `tools/register_key.ps1 [-Key ...]`: registers the player's own CD key
  through the game's `mgspid.dll!GetPidx` dialog, typing it in when given.
  The game quits silently without it. `Setup.cmd` step 4 asks for the EULA
  and the key (`-Key`, `-AcceptEula` to answer in advance).
- Host: `CreateWindowExW` is hidden in `--headless` too; the game's window
  used to appear on the desktop. Conformance 6/8 (window, D3D9 device).
- `run_lift.py`: entries whose code a lifted body already owns (aliases, and
  starts the walk before them falls into) lift as stubs into that body (needs
  pcrecomp #151). 33.4M lines -> 8.0M, 394 no-terminator bodies -> 5.
- The full lift builds on netlab and runs past the CRT, Rockall's heap and the
  x87 dispatch to the game's own "strings.big is corrupt" box (needs pcrecomp
  #150, #152). Conformance: 4/8 milestones, with a new one for that dialog.
- Recatalog and relift on pcrecomp a85ea97: precision 69.9% -> 82.7%, exact
  ends 87.9% -> 94.7%, coverage 95.0% -> 97.8%; 69,558 lifted, 0 errors,
  33.4M lines. No-terminator bodies rose 67 -> 394: alias bodies that exit by
  a backward branch into their parent, not a lift defect (`docs/host.md`).
- `Setup.cmd` / `tools/setup.ps1`: the Quick start. Four discs (drives or
  `.iso`s, mounted natively) + the 2.5 patch -> v2.5 install -> catalog ->
  lift -> optional build and conformance check, skipping finished steps, at
  Idle priority, logged to `setup.log`.
- `tools/conformance.py` + `conformance.json`: boot milestones and lift health,
  failing on regression. First baseline 3/7 milestones, 64,608 lifted, 0 errors.
- `docs/`: architecture, install, recon, catalog, host (moved out of the README).
- `CONTRIBUTING.md`, and a CI workflow for what runs without the game.
- Relift on pcrecomp `main`: catalog P 69.9% / R 99.53% / 87.9% exact ends
  (was 44.9% / 99.27%); `run_lift.py --all` lifts 64,608 functions, 0 errors.
- 32-bit host on pcrecomp `runtime/native32`, adapted from The Movies
  (`run_lift.py`, `CMakeLists.txt`, `build.cmd`, `src/runtime/`).
- Host relaunches itself suspended to reserve 0x00400000-0x014ED000 before the
  loader fills it; map failures list what occupies the range.
- CHANGELOG, ROADMAP.
- Phase 2: function catalog for v2.5 `legends.exe`, scored against IDA 9.1.

### Fixed
- Host: `GetModuleFileNameW`, `GetModuleHandleW` and `GetCommandLineW` answer
  with the guest's `legends.exe`. With the host's path the game moved into
  `build-farm\` and called every `BIGS\*.big` corrupt.

### Changed
- README in the house order (status, Getting Started with both routes, usage,
  build, license); the scope notice moved from LICENSE, which is plain MIT again.
- The install recipe: the four cabinets are independent, so `expand.exe` reads
  each one alone. No third-party extractor is needed for the discs.

### Removed
- `tools/disasm32_owned.py`: upstream disasm32's extent clamping beat it on
  every axis. Its results are in `docs/catalog.md`; the code is at `e68116e`.
- `CLAUDE.md` and `config/symbols_map.json` from tracking. The symbol map is
  reconstructed from the game binary and now lands in `_work/`.
