# Roadmap

## Next
- Relift against current pcrecomp HEAD (disasm32 extent clamping, lift32 fixes).
- Build, and boot to `WinMain` / first window.

## Deferred
- Upstream `disasm32_owned.py` ownership decode (needs `entry_kind` field
  convergence, lift32 alias transfer, and a regression run on a second title).
- `.big` archive reader.

## Out of scope
- Distributing game files, binaries, or generated source.
- The SecuROM-wrapped v1.0 executable (v2.5 is unprotected).
