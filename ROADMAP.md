# Roadmap

## Next
- Recatalog and relift on current pcrecomp `main` (158 commits past the
  86fe9f5 the numbers came from): C++ exceptions for lifted code (eh32, #53),
  guest setjmp/longjmp (#130), sign-flag and x87-compare fixes (#127, `fcom`),
  SSE/SSE2, and catalog fixes (#67, #107, #129). Rescore against IDA.
- Build the full lift on netlab (`netlab build rol`), not the workstation.
- Headless run of the full lift; follow faults to `WinMain` and the first
  window. Add each wall to `docs/host.md`.

## Deferred
- `.big` archive reader.
- The ~6,650 "invented" catalog entries: data, or functions IDA missed.
- A native reference run of v2.5 on real Windows, recorded, as ground truth
  for frame comparisons.

## Out of scope
- Distributing game files, binaries, catalogs, symbol maps or generated source.
- The SecuROM-wrapped v1.0 executable (v2.5 is unprotected).
