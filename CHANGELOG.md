# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added
- Relift on pcrecomp `main`: catalog P 69.9% / R 99.53% / 87.9% exact ends
  (was 44.9% / 99.27%); `run_lift.py --all` lifts 64,608 functions, 0 errors.
- 32-bit host on pcrecomp `runtime/native32`, adapted from The Movies
  (`run_lift.py`, `CMakeLists.txt`, `build.cmd`, `src/runtime/`).
- Host relaunches itself suspended to reserve 0x00400000-0x014ED000 before the
  loader fills it; map failures list what occupies the range.
- CHANGELOG, ROADMAP.
- Phase 2: function catalog for v2.5 `legends.exe`, scored against IDA 9.1.
- `tools/disasm32_owned.py`: ownership-aware decode, duplication 5.56x -> 1.00x.

### Removed
- `CLAUDE.md` and `config/symbols_map.json` from tracking. The symbol map is
  reconstructed from the game binary and now lands in `_work/`.
