# Rise of Legends — Static Recompilation

Static recompilation of **Rise of Nations: Rise of Legends** (Big Huge Games /
Microsoft Game Studios, 2006) from its final official Win32 executable to
native C. Bring your own discs: nothing from the game is in this repository.

Rise of Legends has never been re-released (no Steam, GOG or Store edition),
its DirectX 9 renderer argues with modern drivers, and its online service is
gone. It is also the largest and newest binary the
[pcrecomp](https://github.com/sp00nznet/pcrecomp) toolchain has been pointed
at (13.25 MB of MSVC 7.1 C++, no RTTI for the game's classes), so the second
goal is an audit: every stage is measured against a reference, and what the
tools get wrong goes back upstream. This repo follows pcrecomp's shared house
style (layout, CLI, harness, headless mode), next to
[The Movies](https://github.com/sp00nznet/themovies), its closest sibling.

## Status: **v0.1.0-dev, alpha (bring-up). The whole game lifts, builds and runs its own start-up, to its own error box; nothing renders yet.**

| Stage | State |
|---|---|
| Target build | done: **v2.5**, unprotected; Microsoft's last patch removed the disc protection ([install.md](docs/install.md), [recon.md](docs/recon.md)) |
| Install from discs | done, scripted: 4 discs + the 2.5 patch -> `legends.exe` md5 `b0ebd5c3...`, no installer, no CD key |
| Function catalog (`disasm32`) | 70,864 functions, 30 min; against IDA 9.1: **recall 99.55%, precision 81.3%, exact ends 94.0%** ([catalog.md](docs/catalog.md)) |
| Symbols from the binary's own strings | 143 functions named along the startup path |
| Lift (`run_lift.py --all --split 150`) | **70,867 functions, 0 errors**, 8.0M lines of C; 31,820 alias entries enter their parent's body instead of copying it ([host.md](docs/host.md#the-lift)) |
| Host (`build/rol.exe`, 32-bit, pcrecomp `native32`) | maps the image at `0x00400000`, binds 429/429 imports to the real DLLs, runs the lifted CRT ([host.md](docs/host.md)) |
| Full build | **done on netlab**: clang-cl, 476 objects, about an hour on 12 cores -> `build-farm/rol.exe` |
| Headless mode | `--headless --record out.mp4 --frames N` (from The Movies' host) |
| Conformance harness | `tools/conformance.py`: **6/8 boot milestones** (mapped, bound, entered, its window, a D3D9 device), 0 lift errors, against `conformance.json` |

Where it stops: a fault in lifted code right after the D3D9 device is created
([host.md](docs/host.md#where-it-stops-now-a-fault-after-the-d3d9-device)).

Built on pcrecomp `main` (a85ea97) plus three open pull requests this game
turned up: **#150** (`cmpxchg8b` was a no-op: the heap's lock spun forever),
**#151** (alias entries enter their parent instead of copying it: 75% of the C
was copies), **#152** (a jump-table target on a label inside another handler).
Until they merge, build against a pcrecomp with all three merged in.

## Screenshots

None yet: the game does not reach its first window. Screenshots go in
`docs/screenshots/` when it does.

## Getting Started

You need **your own Rise of Legends discs** (the EN 4-CD retail set, or `.iso`
images of them) and the **2.5 patch** (`RoL_Patch2.5.exe`). Nothing from the
game is in this repository and nothing is downloaded for you.

### Quick start

1. Download this repository (the green **Code** button, then **Download ZIP**)
   and unzip it somewhere with 25 GB free.
2. Double-click **`Setup.cmd`**.

It checks for Python 3.10+, the `pefile` and `capstone` packages, the pcrecomp
toolkit and (for the patch) `innoextract`, and **asks** before installing any
of them. It asks for the folder holding your disc images, or for each disc in
turn, and for the patch. Then it assembles the install and patches it to
v2.5. It asks whether you accept the game's EULA, and for the product key from
your disc case, which it registers with the game's own key check (pass
`Setup.cmd -Key XXXXX-XXXXX-XXXXX-XXXXX-XXXXX` to skip that prompt). Then it
builds the function catalog (about an hour) and lifts it. It asks before the
build, which takes an hour or more and most of your memory. A rerun skips the
finished steps. If it stops, it says why in one sentence, and the details are
in `setup.log`. Everything runs at low priority.

There is nothing to play yet, so it makes no shortcut.

### Step by step

Prerequisites: Windows 10/11, **Python 3.10+** (`py -3 --version` should say
`Python 3.10` or later), **git**, **innoextract 1.9+**
(`winget install dscharrer.innoextract`), and pcrecomp cloned **beside** this
repository as `pcrecomp-rol`:

```
some-folder\
  pcrecomp-rol\   <- git clone https://github.com/sp00nznet/pcrecomp pcrecomp-rol
  rol\            <- this repository
```

Why each install step is the way it is: [docs/install.md](docs/install.md).
Below, `D:\` is wherever each disc is (a drive, or a mounted `.iso`).

1. Python packages:
   ```
   py -3 -m pip install --user pefile capstone
   ```
2. The loose files from discs 1 and 2:
   ```
   robocopy "D:\program files\Microsoft Games\Rise Of Legends" _work\game /E
   ```
   (once with disc 1 in `D:`, once with disc 2)
3. The installer's tables (disc 1), then each disc's cabinet (all four):
   ```
   powershell -ExecutionPolicy Bypass -File tools\dump_msi.ps1 "D:\Rise Of Legends.msi" _work\msi
   expand -F:* D:\Disk1C~1.cab _work\cabstage
   expand -F:* D:\Disk2C~1.cab _work\cabstage
   expand -F:* D:\Disk3C~1.cab _work\cabstage
   expand -F:* D:\Disk4C~1.cab _work\cabstage
   ```
   Make the two folders first (`mkdir _work\msi _work\cabstage`). Expected:
   `_work\msi\file.tsv : 3538 rows`, and 2,595 files in `_work\cabstage`.
4. Put them where the installer would:
   ```
   py -3 tools\place.py _work\msi _work\cabstage _work\game
   ```
   Expected: `placed 2586, outside the game tree 9, not in cabs 943` (the 943 are the loose files from step 2).
5. Patch to v2.5 with the game's own RTPatch (`RunAsInvoker` stops Windows
   elevating it):
   ```
   innoextract -e -m -d _work\patch25 RoL_Patch2.5.exe
   mkdir _work\rtp
   copy _work\game\patch.exe _work\rtp\rtp.exe
   copy _work\game\patchw32.dll _work\rtp\
   set __COMPAT_LAYER=RunAsInvoker
   _work\rtp\rtp.exe _work\game _work\patch25\app\patches\RETAIL-0612.1201.0000-0.0704.1001.0000.RTP
   certutil -hashfile _work\game\legends.exe MD5
   ```
   Expected: `b0ebd5c3154ffd6c0e779d77183000a0`.
6. Your license, which the MSI would otherwise ask for. Read
   `_work\game\EULA.RTF`; if you accept it, record that the way its **Accept**
   button does. Then register the product key from your disc case with the
   game's own key check (Windows asks for admin once):
   ```
   reg add "HKCU\Software\Microsoft\Microsoft Games\Rise of Legends" /v FIRSTRUN /t REG_DWORD /d 1 /f
   powershell -ExecutionPolicy Bypass -File tools\register_key.ps1 -Key XXXXX-XXXXX-XXXXX-XXXXX-XXXXX
   ```
   Expected: `Registered. Product ID: 77277-124-...`. Without `-Key` it opens
   the key dialog for you to type into. Without a product ID the game quits
   at start-up with no message ([host.md](docs/host.md#the-product-key)).
7. The vtables RTTI names, then the function catalog (about an hour):
   ```
   py -3 ..\pcrecomp-rol\tools\cpp\rtti.py _work\game\legends.exe -o _work\relift\rtti.json --seeds _work\relift\rtti_seeds.json
   py -3 -u ..\pcrecomp-rol\tools\disasm\disasm32.py _work\game\legends.exe -o _work\relift\functions.json --seed-functions _work\relift\rtti_seeds.json
   ```
8. Lift it (about half an hour):
   ```
   py -3 run_lift.py --all --split 150
   ```

The usual trip-ups: `python` opening the Microsoft Store (that is Windows'
alias placeholder; use `py -3`, or turn the alias off in *Settings > Apps >
Advanced app settings > App execution aliases*); a freshly installed tool not
being on `PATH` until you open a new window; and disc images that are `.bin` or
`.mdf` rather than `.iso` ([install.md](docs/install.md#disc-images-in-other-formats)).

## Usage

After *Building from source*, from the repository root:

```
build\rol.exe --headless --run --watchdog 60            # boot, report where it stops
build\rol.exe --headless --run --record _work\rec\boot.mp4 --frames 900
build\rol.exe --help                                    # every option
py -3 tools\conformance.py                              # how far it boots, against conformance.json
```

`--headless` keeps everything off the screen (a hidden window, a forced
windowed D3D9 device, message boxes to stderr), so it is safe over RDP. Today a
run maps the image, binds every import, enters the entry point and stops at the
first function the build did not include:

```
[bind] 0x00400000: 417 native, 0 guest, 12 shimmed, 0 unresolved
  mapped _work\game\legends.exe: 0x00400000-0x014ED000
  entering 0x01066ED2

[not-lifted] sub_010675AB  (called from 0x01065264)
```

## Building from source

Needs the *Step by step* outputs plus **Visual Studio 2022** (any edition, or
the Build Tools) with the C++ x86 tools, and **CMake 3.20+** with Ninja (both
come with VS's C++ workload). The host is 32-bit on purpose: the guest image
sits 1:1 at `0x00400000` and calls the game's own 32-bit DLLs
([host.md](docs/host.md)).

```
py -3 run_lift.py                 # quick: the 3,000-function closure from the entry point (~4 min)
py -3 run_lift.py --all --split 150   # everything (33.4M lines, not committed)
build.cmd -j 4                    # vcvarsall amd64_x86 + CMake + Ninja -> build\rol.exe
```

The full lift is big enough to matter: at 12 jobs `cl` ran out of memory, and
at 6 it made the machine unusable. Build it on
[netlab](https://github.com/sp00nznet/recomp-netlab) (`netlab build rol`,
recipe `projects/rol.env`), or locally with a capped `-j` at low priority
(`start /low /b /wait build.cmd -j 4`). `PCRECOMP` points the lifter and
`-DPCRECOMP=` the build at another toolkit checkout; they must match.

Generated source is never distributed: `src/recomp/gen/` is derived from your
own copy of the game and is gitignored.

## Layout

```
rol/
  Setup.cmd           the Quick start (runs tools\setup.ps1)
  run_lift.py         lift driver: catalog -> src\recomp\gen\ (closure or --all)
  CMakeLists.txt, build.cmd   the 32-bit host build
  src/runtime/        host.c (address space, mapping, headless shims, fault report), input.c
  src/recomp/gen/     lifted C (generated, gitignored)
  tools/
    setup.ps1         the same steps as "Step by step", with checks and a log
    dump_msi.ps1, place.py   the installer's tables -> a real directory tree
    symbols.py        names from the binary's own diagnostic strings (--selftest)
    conformance.py    boot milestones and lift health against conformance.json
  config/             measurements: PE headers, disc catalog, recovery scores
  docs/
    architecture.md   the parts and the data flow
    install.md        discs -> v2.5: cabinets, MSI tables, RTPatch
    recon.md          the binary, the engine, middleware, provenance
    catalog.md        the function catalog scored against IDA, and the splits
    host.md           the 32-bit host, the address-space wall, the build
  _work/              your install, the catalog, symbol maps (gitignored)
```

The catalog, symbol maps and lifted code are derived from the game's binary,
so they are never committed. Generic tools live in pcrecomp and are not
duplicated here.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md): where the gaps are, and why game files
and lifted code never go in the repo.

## License

MIT for what is ours: the scripts, the host, the harness and the documentation
([LICENSE](LICENSE)). It does not and cannot cover Rise of Legends itself. No
game code, binaries or extracted assets are committed here, and you need your
own copy for any of this to be useful. The lifted C is a derivative work of
that binary; it is generated from your copy and carries whatever license the
original carries, not this one.

## Credits

Rise of Nations: Rise of Legends © 2006 Microsoft Corporation, developed by Big
Huge Games. This project is not affiliated with or endorsed by either, and
neither contains nor distributes any part of the game.

Built on [pcrecomp](https://github.com/sp00nznet/pcrecomp), which stands on
[capstone](https://www.capstone-engine.org/) and
[pefile](https://github.com/erocarrera/pefile). IDA Pro 9.1 was the scoring
reference. The audit method is
[Trespasser's](https://github.com/sp00nznet/trespasser).
