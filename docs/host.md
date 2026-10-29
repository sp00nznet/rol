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

| | Oct 2 (86fe9f5, `--split 400`) | Oct 28, copies | **Oct 28, alias stubs** |
|---|---|---|---|
| functions | 64,608 | 69,558 | **70,867** |
| errors | 0 | 0 | 0 |
| no terminator | 67 | 394 | **5** |
| unresolvable ITAIL labels | 30 | 3 | **1** |
| lines of C | 22.5M in 162 files | 33.4M in 464 files | **8.0M in 473 files** |
| time | ~25 min | 9 min | 2.5 min |

The last column is pcrecomp `rol/integration`: `main` (a85ea97) plus
pcrecomp #150, #151 and #152, the three fixes below.

### Aliases were 75% of the C

The first full build on netlab died after 33 minutes: `recomp_0160.c`, 245 MB,
was killed for memory on a 63 GB builder. Measured per function:

```
start     39323 functions    405.8 MB  25.4%
alias     30201 functions   1192.3 MB  74.6%
IDA functions lifted most (C MB, entries, IDA size):
  0x008BC530-0x008C62DE    202.4 MB   118 entries   40366 bytes of x86
```

An alias is an entry inside another function's code. Lifted as its own body, it
is a copy of everything from that address to the end of its parent, so one
40 KB function became 202 MB of C. Now `run_lift.py` walks the real starts
first, records which body owns each instruction, and lifts every entry whose
code a lifted body owns as a stub (pcrecomp #151):

```
void sub_0040B55B(void) { g_alias_entry = 0x0040B55Bu; sub_0040B470(); }
```

`sub_0040B470`'s prologue sees `g_alias_entry` and jumps through its local
dispatch, `case 0x0040B55Bu: goto L_0040B55B;`. 31,820 entries are stubs. A
start that the walk before it falls into is stubbed the same way, but only into
a body that is lifted itself, never into another stub.

What is left: a cluster of large functions at `0x008BC530` whose walks overlap
past each other (each tail-jumps to the next, and each walk runs on through
the shared code). Its chunk is 27 MB and builds.

### The 394 bodies "with no terminator" are not a regression.** `true_extent`
calls a body clean only when some path reaches `ret`, `int3`, a tail call or
another entry. A direct jump *backward*, below the body's own start, is none of
those. The new catalog emits twice as many alias entries (15,331 -> 30,201),
and the lift gives each one its own small body. Classified one by one:

```
  377  alias, exits only by a backward branch, inside an IDA function
   12  start, exits only by a backward branch, inside an IDA function
    4  alias, ends in call 0x0106A6BD (_CxxThrowException), then padding
    1  alias, exits only by a backward branch, outside IDA's functions
```

The typical one is a block MSVC moved after its function's `ret`:

```
   0x0040B55A  ret
  >0x0040B55B  cmp edi, ebx
   0x0040B55D  je 0x40b4e7          ; back into the parent
   0x0040B55F  lea ecx, [esp + 0x10]
   0x0040B563  push ecx
   0x0040B564  jmp 0x40b4bd         ; back into the parent
```

The lift turns those backward branches into dispatches (`RECOMP_ITAIL`), and
only 3 of them cannot resolve, so the bodies run. With alias stubs, 389 of
them are stubs and the count is 5.

## The second wall: the heap's lock spun forever (cmpxchg8b)

The full build ran past the CRT, then sat in one function for the whole
watchdog:

```
[watchdog] 60 s: in sub_01061D20, last native call KERNEL32.dll!InterlockedIncrement, 469397165 indirect calls
```

The strings around it name the game's allocator: **Rockall** (Microsoft
Research's heap: `ROCKALL_FRONT_END`, `SHARELOCK`). `0x01061D20` is a SHARELOCK
claim. Its loop only reaches the path that never waits when the user count is
already 0 or less on entry, so the count had been released more than claimed.
Rockall's counters use the real `kernel32` Interlocked calls, but its lock-free
free lists use `lock cmpxchg8b`, and pcrecomp lifted that as

```
; /* UNIMPLEMENTED: lock cmpxchg8b qword ptr [esi] */
```

The `cmp eax, ebp; jne retry` after it still passed, so every push and pop
"succeeded" with memory unchanged. pcrecomp #150 implements it (only ZF
changes); difftest 374/374 against Unicorn.

## The third wall: the CRT's x87 dispatch jumped to a label

```
ITAIL: unresolved VA 0x0106E7AD from 0x0106E6A0
```

`0x0106E6A0` is the CRT's `_trandisp`: `fxam`, `xlatb`, then
`jmp [edx+eax+0x10]` into an unaligned table at `0x0124A7E6`,
`{0x0106FA21, 0x0106E7AD, 0x0106E7B2, 0x0106FB61}`. `0x0106E7AD` is the second
`fstp st(0)` of the `0x0106E7AB` tail, which falls into it, so disasm32's data
scan dropped it as a label. pcrecomp #152 keeps a pointer that sits in a run of
code pointers as an entry (an alias); it kept 678 on this binary.

## Where it stops now: "strings.big is corrupt"

```
[messagebox] type 0x10 -> 1: Fatal String Error: The following files are either corrupt or missing:

.\BIGS\strings.big
.\BIGS\nonloc_strings.big

The program will now exit.
```

Both files are there (6,317,911 and 286,141 bytes) and the original game reads
them, so a lifted path that opens, reads or checks them is wrong. Not
diagnosed yet. Still unimplemented in the lift and worth suspecting: 1,088
`pmaddwd`, 384 `punpck[lh]dq`, 35 `paddsw` (MMX), 6 `pushf`, 4 `popf`.

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
