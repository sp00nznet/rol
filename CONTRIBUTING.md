# Contributing to the Rise of Legends recomp

This repository is the game-specific half: the install scripts, the lift
driver, the host and the notes. The recompiler itself is
[pcrecomp](https://github.com/sp00nznet/pcrecomp), and most of what this game
finds belongs there. Contributions to either are welcome.

## Where the gaps are

| Area | What is missing | Difficulty |
|---|---|---|
| **The full build** | Building 22.5M lines of lifted C within a normal machine's memory: chunk size, `/O1`, or a smarter split. | Medium |
| **Boot** | Following faults from the CRT to `WinMain` and the first window ([docs/host.md](docs/host.md)). | Large, incremental |
| **`.big` archives** | A reader for `BIGS/*.big` (the binary's `BigFile` class). | Medium |
| **The "invented" catalog entries** | ~4,750 entries IDA does not call functions: which are data and which are functions IDA missed ([docs/catalog.md](docs/catalog.md)). Fixes go to pcrecomp's `disasm32`. | Medium |

## Ground rules

**No game files, ever.** No executables, DLLs, `.big`s, video or audio. No
lifted code, catalogs or symbol maps either: `_work/` and `src/recomp/gen/` are
generated from your own copy and gitignored, and must stay that way.
Screenshots of what the recompiled game renders are fine.

**Where your code comes from.** Contributions must be your own work or under
an MIT-compatible license. The easy mistake is porting a fix you saw in a GPL
project (Wine, ReactOS, DOSBox, a GPL decompilation), which relicenses it by
accident and is very hard to untangle later. If you port anything, say where
it came from in the pull request.

**Generic fixes go upstream.** If the bug is in the lifter, the catalog or the
runtime, it belongs in pcrecomp as its own PR, and this repo only picks it up.
Game-specific values and shims stay here.

**Say what you measured.** Show the boot before and after
(`py -3 tools\conformance.py`), and the log line that changed. Add the real
output for a new wall to [docs/host.md](docs/host.md).

**AI-assisted contributions** are welcome, provided a human understood and
verified the change.

## Before you open a pull request

With your own copy set up (README, Getting Started):

```
py -3 run_lift.py --all --split 150
build.cmd -j 4
py -3 tools\conformance.py
```

`conformance.py` fails on a regression against `conformance.json`. Update the
baseline (`--update`) only when the change is meant to move it, and say so.
