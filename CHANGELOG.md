# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added
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
