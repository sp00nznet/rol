# Roadmap

## Next
- Build the full lift on netlab (`netlab build rol`), not the workstation.
- Headless run of the full lift; follow faults to `WinMain` and the first
  window. Add each wall to `docs/host.md`.

## Deferred
- `.big` archive reader.
- The ~4,750 "invented" catalog entries: data, or functions IDA missed.
- A native reference run of v2.5 on real Windows, recorded, as ground truth
  for frame comparisons.

## Out of scope
- Distributing game files, binaries, catalogs, symbol maps or generated source.
- The SecuROM-wrapped v1.0 executable (v2.5 is unprotected).
