# One-click setup for the Rise of Legends static recompilation. Run it by
# double-clicking Setup.cmd in the repo folder; the README's "Step by step" is
# the same thing by hand, command for command, and docs/install.md says why
# each install step is the way it is.
#
# The project is in bring-up (README, Status): this takes your discs as far as
# the pipeline goes today. Assemble the install from the four discs, patch it
# to v2.5, build the function catalog, lift it, and (if you say yes) build the
# host and run the conformance check. Nothing plays yet, so there is no
# shortcut at the end. Each step is skipped when its output already exists
# (-Force redoes them). Everything it does goes to setup.log.
#
# The whole script runs at Idle priority, and so does everything it starts:
# the catalog and the build take every core they are given, and the machine
# should stay usable while they do.
param([switch]$Force, [string]$Discs = "", [string]$Patch = "")

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Toolkit = if ($env:PCRECOMP) { $env:PCRECOMP } else { Join-Path (Split-Path -Parent $Root) 'pcrecomp-rol' }
$T = Join-Path $Toolkit 'tools'
$Log = Join-Path $Root 'setup.log'
$V25 = 'B0EBD5C3154FFD6C0E779D77183000A0'
$Delta = 'RETAIL-0612.1201.0000-0.0704.1001.0000.RTP'
Set-Location $Root
(Get-Process -Id $PID).PriorityClass = 'Idle'   # inherited by every child process
function Log($t) { Add-Content -Path $Log -Value $t -Encoding UTF8 }
Log "==== setup $(Get-Date -Format s)"

$Mounted = @()
function Dismount-All { foreach ($i in $script:Mounted) { try { Dismount-DiskImage -ImagePath $i | Out-Null } catch {} } }
function Say($t, $c = 'Gray') { Write-Host $t -ForegroundColor $c; Log $t }
function Step($n, $t) { Write-Host ""; Say "[$n/7] $t" 'Cyan' }
function Fail($t) {
  Dismount-All
  Say "" ; Say "Setup stopped: $t" 'Red'
  Say "The details are in $Log. Fix that and run Setup.cmd again; finished steps are skipped." 'Yellow'
  Read-Host "Press Enter to close" | Out-Null; exit 1
}
function Ask($q) { $a = Read-Host "$q [Y/n]"; return -not ($a -match '^[nN]') }   # Enter = yes
function Refresh-Path {
  $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}
# Run a program with its output in the log. Windows PowerShell turns a native
# program's stderr into errors, so 'Stop' is off while it runs.
function Exec([string[]]$cmd) {
  $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
  $rest = @($cmd | Select-Object -Skip 1)   # @(): a one-element slice would splat as characters
  & $cmd[0] @rest 2>&1 | ForEach-Object { Log "$_" }
  $code = $LASTEXITCODE
  $ErrorActionPreference = $old
  return $code
}
function Run($what, [string[]]$cmd) {
  Say "  $what..."
  $code = Exec $cmd
  if ($code -ne 0) { Fail "$what failed (exit code $code)." }
}
function Md5($f) { if (Test-Path $f) { (Get-FileHash $f -Algorithm MD5).Hash } else { '' } }

Clear-Host
Say "Rise of Legends: static recompilation setup" 'White'
Say "You need your four Rise of Legends discs (or .iso images of them), the 2.5 patch"
Say "(RoL_Patch2.5.exe), and about 25 GB free. The catalog step takes about an hour, once."

# ---------------------------------------------------------------- tools
Step 1 "Checking the tools the pipeline needs"
# A Python that answers "Python 3.x". The Store's placeholder "python" (the
# one that opens the Store) answers nothing, so asking is the reliable test.
function Find-Python {
  foreach ($c in @(@('py', '-3'), @('python'), @('python3'))) {
    if (-not (Get-Command $c[0] -ErrorAction SilentlyContinue)) { continue }
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $rest = @($c | Select-Object -Skip 1)
    $v = (& $c[0] @rest --version 2>&1 | Out-String).Trim()
    $ErrorActionPreference = $old
    if ($v -match '^Python 3\.(\d+)' -and [int]$Matches[1] -ge 10) { return ,$c }
  }
  return $null
}
$pyargs = Find-Python
if (-not $pyargs) {
  Say "  Python 3.10 or newer is not installed."
  if (-not (Ask "  Install Python 3.12 now (winget, for your user only, about 30 MB)?")) { Fail "Python 3 is required." }
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Fail "winget is missing. Install 'App Installer' from the Microsoft Store, or install Python yourself."
  }
  Exec @('winget', 'install', '-e', '--id', 'Python.Python.3.12', '--scope', 'user', '--accept-package-agreements', '--accept-source-agreements') | Out-Null
  Refresh-Path
  $pyargs = Find-Python
  if (-not $pyargs) { Fail "Python installed, but Windows has not picked it up yet: close this window and run Setup.cmd again." }
}
Say "  Python: $($pyargs -join ' ')"

if ((Exec ($pyargs + @('-c', 'import pefile, capstone'))) -ne 0) {
  Say "  The Python packages pefile and capstone are missing (about 15 MB)."
  if (-not (Ask "  Install them now (pip, for your user only)?")) { Fail "pefile and capstone are required." }
  Run "Installing pefile and capstone" ($pyargs + @('-m', 'pip', 'install', '--user', 'pefile', 'capstone'))
}

if (-not (Test-Path (Join-Path $T 'lift\lift32.py'))) {
  Say "  The pcrecomp toolkit is not beside this folder ($Toolkit)."
  if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "git is needed to fetch pcrecomp. Install Git for Windows, then run Setup.cmd again." }
  if (-not (Ask "  Download it now (git, about 20 MB)?")) { Fail "pcrecomp is required at $Toolkit." }
  Run "Cloning pcrecomp" @('git', 'clone', '--depth', '1', 'https://github.com/sp00nznet/pcrecomp', $Toolkit)
}
Say "  pcrecomp: $Toolkit"

# ---------------------------------------------------------------- discs
Step 2 "Assembling the v1.0 install from your discs into _work\game (docs\install.md)"
$Game = Join-Path $Root '_work\game'
$Exe = Join-Path $Game 'legends.exe'
# A disc is any drive or folder with Disk<N>C~1.cab at its root.
function Find-Disc($n) {
  foreach ($r in [IO.DriveInfo]::GetDrives().Name) {   # sees letters mounted after start, unlike Get-PSDrive
    if (Test-Path -LiteralPath (Join-Path $r "Disk${n}C~1.cab")) { return $r }
  }
  return $null
}
function Mount-Iso($iso) {
  $img = Get-DiskImage -ImagePath $iso
  if (-not $img.Attached) { Mount-DiskImage -ImagePath $iso | Out-Null; $script:Mounted += $iso }
  return ((Get-DiskImage -ImagePath $iso | Get-Volume).DriveLetter + ':\')
}
function Get-Disc($n) {
  while ($true) {
    $d = Find-Disc $n
    if ($d) { return $d }
    $p = (Read-Host "  Disc $n`: insert it and type its drive letter, or paste the path of its .iso").Trim('"', ' ')
    if ($p -match '^[A-Za-z]:?\\?$') { continue }            # a drive letter: Find-Disc looks there
    if ($p -match '\.iso$' -and (Test-Path -LiteralPath $p)) { Mount-Iso (Resolve-Path -LiteralPath $p).Path | Out-Null }
    else { Say "  That is not a drive with Disk${n}C~1.cab on it, or an .iso file." 'Yellow' }
  }
}

if ((Test-Path $Exe) -and -not $Force) {
  Say "  Already in _work\game (skipping)."
} else {
  if (-not $Discs) { $Discs = (Read-Host "  Folder holding your disc .iso files (Enter if the discs are in a drive)").Trim('"', ' ') }
  if ($Discs -and (Test-Path -LiteralPath $Discs)) {
    foreach ($iso in Get-ChildItem -LiteralPath $Discs -Filter *.iso) { Mount-Iso $iso.FullName | Out-Null }
  }
  $Stage = Join-Path $Root '_work\cabstage'
  New-Item -ItemType Directory -Force $Game, $Stage, (Join-Path $Root '_work\msi') | Out-Null
  foreach ($n in 1..4) {
    $d = Get-Disc $n
    Say "  Disc $n at $d"
    $loose = Join-Path $d 'program files\Microsoft Games\Rise Of Legends'
    if (Test-Path -LiteralPath $loose) {
      Say "  Copying disc $n's loose files..."
      $code = Exec @('robocopy', $loose, $Game, '/E', '/NFL', '/NDL', '/NJH', '/NP')
      if ($code -ge 8) { Fail "copying disc $n failed (robocopy exit code $code)." }   # robocopy: <8 is success
    }
    if ($n -eq 1) {
      Run "Reading the installer's tables" @('powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        (Join-Path $Root 'tools\dump_msi.ps1'), (Join-Path $d 'Rise Of Legends.msi'), (Join-Path $Root '_work\msi'))
    }
    # The four cabinets are independent (no chain), so expand reads each alone.
    Run "Extracting disc $n's cabinet" @('expand', '-F:*', (Join-Path $d "Disk${n}C~1.cab"), $Stage)
  }
  Run "Placing the cabinet files" ($pyargs + @('tools\place.py', '_work\msi', '_work\cabstage', '_work\game'))
  Dismount-All
  if (-not (Test-Path $Exe)) { Fail "the assembled install has no legends.exe." }
}

# ---------------------------------------------------------------- patch
Step 3 "Patching to v2.5 with Microsoft's own RTPatch delta"
if ((Md5 $Exe) -eq $V25) {
  Say "  legends.exe is already v2.5 (skipping)."
} else {
  $Rtp = Join-Path $Root "_work\patch25\app\patches\$Delta"
  if (-not (Test-Path $Rtp)) {
    $cands = @($Patch, (Join-Path $Root 'RoL_Patch2.5.exe'), (Join-Path $Root "_work\RoL_Patch2.5.exe"),
               (Join-Path (Split-Path -Parent $Root) 'RoL_Patch2.5.exe')) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
    $Patch = @($cands)[0]
    while (-not $Patch) {
      $p = (Read-Host "  Paste the path of RoL_Patch2.5.exe (or of $Delta)").Trim('"', ' ')
      if ($p -and (Test-Path -LiteralPath $p)) { $Patch = $p } else { Say "  No file there." 'Yellow' }
    }
    if ($Patch -match '\.rtp$') { $Rtp = $Patch }
    else {
      if (-not (Get-Command innoextract -ErrorAction SilentlyContinue)) {
        Say "  innoextract is not installed. It unpacks the patch's Inno Setup wrapper without running it."
        if (-not (Ask "  Install innoextract 1.9 now (winget, about 5 MB)?")) { Fail "innoextract is required to unpack $Patch." }
        Exec @('winget', 'install', '-e', '--id', 'dscharrer.innoextract', '--accept-package-agreements', '--accept-source-agreements') | Out-Null
        Refresh-Path
        if (-not (Get-Command innoextract -ErrorAction SilentlyContinue)) { Fail "innoextract installed, but Windows has not picked it up yet: close this window and run Setup.cmd again." }
      }
      Run "Unpacking the patch" @('innoextract', '-e', '-m', '-d', (Join-Path $Root '_work\patch25'), $Patch)
      if (-not (Test-Path $Rtp)) { Fail "the patch holds no $Delta. Is it the 2.5 patch?" }
    }
  }
  # The game's own patch.exe applies it. Windows auto-elevates anything named
  # patch.exe (its version resource too), so it runs as rtp.exe with
  # RunAsInvoker. RTPatch is transactional: a failed run changes nothing.
  $R = Join-Path $Root '_work\rtp'
  New-Item -ItemType Directory -Force $R | Out-Null
  Copy-Item (Join-Path $Game 'patch.exe') (Join-Path $R 'rtp.exe') -Force
  Copy-Item (Join-Path $Game 'patchw32.dll') $R -Force
  $env:__COMPAT_LAYER = 'RunAsInvoker'
  Run "Applying $Delta" @((Join-Path $R 'rtp.exe'), $Game, $Rtp)
  Remove-Item Env:\__COMPAT_LAYER
  $h = Md5 $Exe
  if ($h -ne $V25) { Fail "after patching, legends.exe has md5 $h, not $V25." }
}
Say "  legends.exe v2.5, md5 $V25" 'Green'

# ---------------------------------------------------------------- analyze
$W = '_work\relift'
New-Item -ItemType Directory -Force $W | Out-Null
Step 4 "Finding the C++ vtables the binary does name (seconds)"
if ((Test-Path "$W\rtti_seeds.json") -and -not $Force) { Say "  Already done (skipping)." }
else { Run "RTTI" ($pyargs + @("$T\cpp\rtti.py", '_work\game\legends.exe', '-o', "$W\rtti.json", '--seeds', "$W\rtti_seeds.json")) }

# ---------------------------------------------------------------- catalog
Step 5 "Finding every function (the long one: about an hour, once)"
if ((Test-Path "$W\functions.json") -and -not $Force) { Say "  Already done (skipping)." }
else { Run "Disassembling" ($pyargs + @('-u', "$T\disasm\disasm32.py", '_work\game\legends.exe', '-o', "$W\functions.json", '--seed-functions', "$W\rtti_seeds.json")) }

# ---------------------------------------------------------------- lift
Step 6 "Lifting every function to C (about half an hour)"
if ((Test-Path "$W\lift_stats.json") -and (Test-Path 'src\recomp\gen\recomp_dispatch.c') -and -not $Force) { Say "  Already done (skipping)." }
else { Run "Lifting" ($pyargs + @('run_lift.py', '--all', '--split', '150')) }

# ---------------------------------------------------------------- build
Step 7 "Building the host (optional: an hour or more, and most of your memory)"
if ((Test-Path 'build\rol.exe') -and -not $Force) { Say "  build\rol.exe is already there (skipping)." }
elseif (Ask "  Build it now? It runs at low priority, but expect a slow machine until it is done.") {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not ((Test-Path $vswhere) -and (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath))) {
    Fail "Visual Studio 2022 (or its Build Tools) with 'Desktop development with C++' is needed to build. Install it, then run Setup.cmd again."
  }
  # Each lifted chunk takes 1-3 GB in cl; one job per 4 cores keeps it in memory.
  $j = [Math]::Max(2, [int]([Environment]::ProcessorCount / 4))
  Run "Building with $j jobs" @('cmd', '/c', 'build.cmd', '-j', "$j")
}
if (Test-Path 'build\rol.exe') {
  Say "  Running the conformance check (a minute)..."
  Exec ($pyargs + @('tools\conformance.py')) | Out-Null
  Get-Content $Log -Tail 12 | Where-Object { $_ -match 'milestones|\[x\]|\[ \]|lift:|REGRESSION' } | ForEach-Object { Write-Host "  $_" }
}

Write-Host ""
Say "Done: _work\ holds your v2.5 install and the catalog, src\recomp\gen\ the lifted C." 'Green'
Say "There is nothing to play yet. The README's Status section says how far the project is."
Read-Host "Press Enter to close" | Out-Null
