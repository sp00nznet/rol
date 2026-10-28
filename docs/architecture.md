# Architecture

Two languages, two repositories, one data flow. This repo holds the
game-specific half; the recompiler is [pcrecomp](https://github.com/sp00nznet/pcrecomp),
checked out beside it as `../pcrecomp-rol` (override with `PCRECOMP`).

```
4 discs + RoL_Patch2.5.exe
   |  tools/setup.ps1: expand, dump_msi.ps1, place.py, innoextract, RTPatch   (docs/install.md)
   v
_work/game/legends.exe (v2.5)
   |  pcrecomp tools/cpp/rtti.py         -> _work/relift/rtti_seeds.json
   |  pcrecomp tools/disasm/disasm32.py  -> _work/relift/functions.json      (docs/catalog.md)
   v
run_lift.py  (pcrecomp generate.py + lift32)
   v
src/recomp/gen/*.c   (generated, gitignored)
   |  CMakeLists.txt + build.cmd, with pcrecomp runtime/native32 + recomp32
   v
build/rol.exe  (32-bit host: src/runtime/host.c)                              (docs/host.md)
   |  maps legends.exe at 0x00400000, binds the real DLLs, runs lifted code
   v
tools/conformance.py  -> conformance.json (boot milestones + lift health)
```

| Part | Owns |
|---|---|
| `tools/setup.ps1` | Getting a v2.5 tree and every derived file onto a clean machine. Skips finished steps. |
| `tools/dump_msi.ps1`, `tools/place.py` | Turning the MSI's cabinet entries into a real directory tree. |
| `tools/symbols.py` | Names from the binary's own diagnostic strings. Output stays in `_work/`. |
| `run_lift.py` | Which functions to lift (closure from the entry, or `--all`) and how many per file. |
| `src/runtime/host.c` | Address-space reservation, image mapping, the headless D3D9 and window shims, scripted input, watchdog and fault reports. |
| `tools/conformance.py` | The pass/fail count against `conformance.json`. |
| pcrecomp | Disassembly, lifting, the CPU model, the native bridge. Generic fixes go there. |

Everything under `_work/`, `src/recomp/gen/` and `build/` is derived from the
user's own copy and is never committed.
