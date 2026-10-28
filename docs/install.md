# From four discs to v2.5, without installing anything

`tools/setup.ps1` (run by `Setup.cmd`) does all of this; the README's *Step by
step* is the same commands by hand. This file is why each step is the way it is.

## Why not just run the installer

The MSI wants a CD key and four disc swaps, writes registry keys, and installs
v1.0, whose `legends.exe` is wrapped in SecuROM-class protection
([recon.md](recon.md)). Microsoft's own final patch replaces that executable
with an unprotected one. So the install is assembled from the discs directly,
then patched.

## 1. The loose files (discs 1 and 2)

Discs 1 and 2 carry part of the game as plain files under
`program files\Microsoft Games\Rise Of Legends\`. Copy both trees into
`_work\game\`.

## 2. The cabinets (all four discs)

Each disc has one `Disk<N>C~1.cab` at its root. The four are **independent**
cabinets: their headers name no previous or next cabinet (flags 0), so
`expand.exe` extracts each on its own. (`extrac32 /A`, which follows cabinet
chains, stops after disc 1 for the same reason.)

```
expand -F:* D:\Disk1C~1.cab _work\cabstage
...
```

Result: 2,595 entries, 1,433,911,237 bytes. They are named by MSI `File` key
(`_0002D1BC05214A65BD461A93869B358F`), not by their real names.

## 3. Placing them through the MSI's own tables

`tools/dump_msi.ps1` reads `Rise Of Legends.msi` (disc 1 root) through the
`WindowsInstaller.Installer` COM object and writes the `File`, `Component`,
`Directory` and `Media` tables as TSV. `tools/place.py` walks the directory
tree to the folder named `Rise Of Legends` (id `INSTALLDIR`) and copies each
entry to its real name and path.

```
_work\msi\file.tsv : 3538 rows
placed 2586, outside the game tree 9, not in cabs 943
```

The 943 "not in cabs" are the loose files from step 1; the 9 "outside" are the
MSXML4 system files. Every placed file matches the size
the MSI records. Result: 2.8 GB, 3,545 files, v1.0, no registry, no CD key,
nothing system-wide.

## 4. The 2.5 patch

The circulating `RoL_Patch2.5.exe` is an unofficial Inno Setup 5.5 wrapper
(German readme) around eleven RTPatch deltas that step build `0604.2001.0000`
(retail, April 2006) to `0704.1001.0000` (April 2007), plus a
`patch_control.xml` giving the order. `innoextract` 1.9 unpacks it (an older
`innounp` cannot):

```
innoextract -e -m -d _work\patch25 RoL_Patch2.5.exe
```

**Only the last delta is needed.** RTPatch 8.10 is transactional: it stages to
temporary files and commits only on success, so each delta could be tried
against an untouched tree. Ten abort with `ept0036` (old file content
mismatch). `RETAIL-0612.1201.0000-0.0704.1001.0000.RTP` (157 MB) applies: it
replaces 51 files wholesale rather than diffing them, so it does not care which
build it starts from.

The applier is the game's own `patch.exe` + `patchw32.dll`:

```
copy _work\game\patch.exe _work\rtp\rtp.exe
copy _work\game\patchw32.dll _work\rtp\
set __COMPAT_LAYER=RunAsInvoker
_work\rtp\rtp.exe _work\game _work\patch25\app\patches\RETAIL-0612.1201.0000-0.0704.1001.0000.RTP
```

**Windows auto-elevates any exe named `patch.exe`** (legacy installer
detection). Renaming it alone does not help: its version resource triggers the
same heuristic. `__COMPAT_LAYER=RunAsInvoker` turns that off for this process
without touching the system.

## 5. Check

```
legends.exe  15,548,416 bytes  md5 b0ebd5c3154ffd6c0e779d77183000a0
```

That size was also one of the unidentified 32-bit fields beside the filename in
the RTPatch container: the new-file-size field. Do not reverse the container;
applying the patch answered every question reversing it raised.

## Disc images in other formats

Setup mounts `.iso` files with Windows' own `Mount-DiskImage`, which works
without admin rights. A raw `.bin` (MODE1/2352) or an Alcohol `.mdf`
(2448 bytes per sector: 2352 + 96 bytes of subchannel) has to become an ISO
first: keep bytes 16..2064 of each sector. pcrecomp's
`tools/assets/bin2iso.js` does the `.bin` case.
