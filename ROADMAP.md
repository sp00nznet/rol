# Roadmap

## Next
- The fault in `sub_00F205A0` after the D3D9 device, then the first frame.
- Follow the boot to the first window. Add each wall to `docs/host.md`.
- Land pcrecomp #150, #151, #152, then pin to `main` again.
- The overlapping walks at `0x008BC530` (one 27 MB chunk).

## Deferred
- `.big` archive reader.
- The ~4,750 "invented" catalog entries: data, or functions IDA missed.
- A native reference run of v2.5 on real Windows, recorded, as ground truth
  for frame comparisons.

## Out of scope
- Distributing game files, binaries, catalogs, symbol maps or generated source.
- The SecuROM-wrapped v1.0 executable (v2.5 is unprotected).
