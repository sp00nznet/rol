# The host: lift, build, first run

The host follows [The Movies](https://github.com/sp00nznet/themovies): a
disasm32 catalog, `run_lift.py` over pcrecomp's `generate.py` + `lift32`, and a
**32-bit MSVC host on pcrecomp's `runtime/native32`**. The guest image sits 1:1
at `0x00400000`, and every import, `GetProcAddress` result and COM vtable is the
real DLL behind one bridge. PhysX, Bink, `fluidModel.dll` and `d3d8xstub.dll`
are called as they are, with no hand-written shims. `SetDllDirectoryA(game)`
lets them load from the game folder.

Code: `src/runtime/host.c`, `CMakeLists.txt`, `build.cmd`.

## The lift

| | Oct 2 (86fe9f5, `--split 400`) | **Oct 28 (a85ea97, `--split 150`)** |
|---|---|---|
| functions | 64,608 | **69,558** |
| errors | 0 | 0 |
| no terminator | 67 | **394** |
| unresolvable ITAIL labels | 30 | **3** |
| lines of C | 22.5M in 162 files | **33.4M in 464 files** |
| time | ~25 min | 9 min |

The new lift is 48% more C, from a catalog that covers 2.8 points more of
`.text` and ends more functions exactly. The bodies with no terminator went
from 67 to 394; that is a regression against `conformance.json` and is not
diagnosed yet.

## The first wall: 0x00400000 was taken

RoL's image runs to `0x014ED000`, 4 MB further than The Movies'. In a host
linked high, the process's own early allocations fill low address space
first-fit: the 16 MB initial stack took `0x00400000-0x01400000`, and the
loader's heaps and mapped NLS files took the rest.

So the host relaunches itself suspended, reserves the range in the child before
its loader runs, and lets `image_loader` commit into the reservation. Releasing
the range first does not work: the loader `malloc`s the 15 MB file before
mapping, and the heap takes the hole. The initial stack is 1 MB, which fits
below the image, and the guest runs on a 16 MB thread. When mapping fails, the
host lists what occupies the range.

## First run (3,000-function closure, headless)

```
[bind] 0x00400000: 417 native, 0 guest, 12 shimmed, 0 unresolved
  mapped _work\game\legends.exe: 0x00400000-0x014ED000
  entering 0x01066ED2

[not-lifted] sub_010675AB  (called from 0x01065264)
last indirect calls (newest first):
  0x761041B0  from 0x010677A2  KERNEL32.dll!HeapFree
  0x776BF650  from 0x010676E9  KERNEL32.dll!HeapAlloc
```

The lifted CRT runs against the real heap and stops exactly at the edge of what
was lifted.

## The full build, and why it stopped

At 12 parallel jobs, `cl` died on 14 of the 30-50 MB chunks with no diagnostic
(out of memory); each compiles alone in about 70 s. At `-j 6` the build made
the workstation unusable and was stopped. The 32-bit `cl.exe` runs out of heap
on a 400-function chunk (C1002), which is why `build.cmd` uses the
64-bit-hosted `amd64_x86` compiler.

What to do instead: smaller chunks (`run_lift.py --all --split 150`), and build
on netlab (`netlab build rol`, recipe `projects/rol.env`, which also passes
`/O1` because clang's `/O2` loop unswitching is quadratic on the largest lifted
functions). On a workstation, cap `-j` and run at low priority.

## Headless

`--headless` hides the window, forces a windowed D3D9 device, and sends message
boxes to stderr, so a run is safe over RDP. `--record out.mp4 --frames N` reads
every frame back and pipes it to ffmpeg. `build\rol.exe --help` lists the rest.
