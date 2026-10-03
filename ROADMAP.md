# Roadmap

## Next
- Finish the full build without starving the workstation: `--split 150`,
  low priority, capped `-j`.
- Headless run of the full lift; follow faults to `WinMain` and the first window.

## Deferred
- Upstream `disasm32_owned.py` ownership decode (needs `entry_kind` field
  convergence, lift32 alias transfer, and a regression run on a second title).
- `.big` archive reader.

## Out of scope
- Distributing game files, binaries, or generated source.
- The SecuROM-wrapped v1.0 executable (v2.5 is unprotected).
