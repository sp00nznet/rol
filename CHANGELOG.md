# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added
- LICENSE (MIT, covers this repo's tooling only), CHANGELOG, ROADMAP.
- Phase 2: function catalog for v2.5 `legends.exe`, scored against IDA 9.1.
- `tools/disasm32_owned.py`: ownership-aware decode, duplication 5.56x -> 1.00x.

### Removed
- `CLAUDE.md` and `config/symbols_map.json` from tracking. The symbol map is
  reconstructed from the game binary and now lands in `_work/`.
