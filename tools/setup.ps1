# One-click setup for the Rise of Legends static recompilation. Run it by
# double-clicking Setup.cmd in the repo folder; the README's "Step by step" is
# the same thing by hand, command for command, and docs/install.md says why
# each install step is the way it is.
#
# It assembles the install from the four discs, patches it to v2.5, builds the
# function catalog, lifts it, builds the host (if you say yes), makes the zoom
# archives the F10 menu uses and leaves 'Rise of Legends (recomp).cmd' to play.
# Each step is skipped when its output already exists (-Force redoes them).
# Everything it does goes to setup.log.
#
# -Game takes the folder holding the four disc .iso files and RoL_Patch2.5.exe.
# -Yes answers every question yes and never waits for input: the EULA counts
# as accepted, the product key is typed into Microsoft's own dialog, and
# anything it would have asked for is a 'Setup stopped' instead. The installer
# (recomp-launcher) runs it so, with RECOMP_LAUNCHER set, which implies -Yes
# and skips the "Press Enter" at the end.
#
# The whole script runs at Idle priority, and so does everything it starts:
# the catalog and the build take every core they are given, and the machine
# should stay usable while they do.
param([switch]$Force, [switch]$Yes, [string]$Game = "", [string]$Discs = "", [string]$Patch = "", [string]$Key = "", [switch]$AcceptEula)

$ErrorActionPreference = 'Stop'
$Auto = $Yes -or [bool]$env:RECOMP_LAUNCHER
if ($Game) { $Game = $Game.Trim('"', ' '); if (-not $Discs) { $Discs = $Game } }
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
$Steps = 10
function Step($n, $t) { Write-Host ""; Say "[$n/$Steps] $t" 'Cyan' }
function Pause-End { if (-not $env:RECOMP_LAUNCHER) { Read-Host "Press Enter to close" | Out-Null } }
function Fail($t) {
  Dismount-All
  Say "" ; Say "Setup stopped: $t" 'Red'
  Say "The details are in $Log. Fix that and run Setup.cmd again; finished steps are skipped." 'Yellow'
  Pause-End; exit 1
}
function Ask($q) { if ($Auto) { Say "$q yes"; return $true }; $a = Read-Host "$q [Y/n]"; return -not ($a -match '^[nN]') }   # Enter = yes
# Read-Host, unless nobody is there to answer: then stop and say why.
function Ask-Text($q, $why) { if ($Auto) { Fail $why }; return (Read-Host $q) }
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
Say "(RoL_Patch2.5.exe), about 25 GB free and 16 GB of memory. The catalog and the build"
Say "take two to four hours, once."

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
$Inst = Join-Path $Root '_work\game'
$Exe = Join-Path $Inst 'legends.exe'
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
    $p = (Ask-Text "  Disc $n`: insert it and type its drive letter, or paste the path of its .iso" "disc $n was not found: no .iso in $Discs has Disk${n}C~1.cab on it.").Trim('"', ' ')
    if ($p -match '^[A-Za-z]:?\\?$') { continue }            # a drive letter: Find-Disc looks there
    if ($p -match '\.iso$' -and (Test-Path -LiteralPath $p)) { Mount-Iso (Resolve-Path -LiteralPath $p).Path | Out-Null }
    else { Say "  That is not a drive with Disk${n}C~1.cab on it, or an .iso file." 'Yellow' }
  }
}

if ((Test-Path $Exe) -and -not $Force) {
  Say "  Already in _work\game (skipping)."
} else {
  if (-not $Discs -and -not $Auto) { $Discs = (Read-Host "  Folder holding your disc .iso files (Enter if the discs are in a drive)").Trim('"', ' ') }
  if ($Discs -and (Test-Path -LiteralPath $Discs)) {
    foreach ($iso in Get-ChildItem -LiteralPath $Discs -Filter *.iso) { Mount-Iso $iso.FullName | Out-Null }
  }
  $Stage = Join-Path $Root '_work\cabstage'
  New-Item -ItemType Directory -Force $Inst, $Stage, (Join-Path $Root '_work\msi') | Out-Null
  foreach ($n in 1..4) {
    $d = Get-Disc $n
    Say "  Disc $n at $d"
    $loose = Join-Path $d 'program files\Microsoft Games\Rise Of Legends'
    if (Test-Path -LiteralPath $loose) {
      Say "  Copying disc $n's loose files..."
      $code = Exec @('robocopy', $loose, $Inst, '/E', '/NFL', '/NDL', '/NJH', '/NP')
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
    $cands = @($Patch, $(if ($Game) { Join-Path $Game 'RoL_Patch2.5.exe' }), (Join-Path $Root 'RoL_Patch2.5.exe'), (Join-Path $Root "_work\RoL_Patch2.5.exe"),
               (Join-Path (Split-Path -Parent $Root) 'RoL_Patch2.5.exe')) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
    $Patch = @($cands)[0]
    while (-not $Patch) {
      $p = (Ask-Text "  Paste the path of RoL_Patch2.5.exe (or of $Delta)" "RoL_Patch2.5.exe was not found beside your disc images ($Game).").Trim('"', ' ')
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
  Copy-Item (Join-Path $Inst 'patch.exe') (Join-Path $R 'rtp.exe') -Force
  Copy-Item (Join-Path $Inst 'patchw32.dll') $R -Force
  $env:__COMPAT_LAYER = 'RunAsInvoker'
  Run "Applying $Delta" @((Join-Path $R 'rtp.exe'), $Inst, $Rtp)
  Remove-Item Env:\__COMPAT_LAYER
  $h = Md5 $Exe
  if ($h -ne $V25) { Fail "after patching, legends.exe has md5 $h, not $V25." }
}
Say "  legends.exe v2.5, md5 $V25" 'Green'

# ---------------------------------------------------------------- EULA + key
# The MSI would ask for both; this install skips the MSI (docs/install.md), and
# the game quits silently without a product ID (docs/host.md).
Step 4 "Your license: the EULA and your product key"
$eulaKey = 'HKCU:\Software\Microsoft\Microsoft Games\Rise of Legends'
$pidKey = 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Microsoft Games\Rise of Legends'
$accepted = $false
try { $accepted = [int](Get-ItemProperty $eulaKey -ErrorAction Stop).FIRSTRUN -ne 0 } catch {}
if ($accepted) { Say "  EULA already accepted (skipping)." }
else {
  Say "  The license agreement is $(Join-Path $Inst 'EULA.RTF')."
  # Under -Yes the player accepted it by pressing Build (the installer says so).
  if (-not ($AcceptEula -or (Ask "  Have you read it, and do you accept it?"))) { Fail "the game does not run until its EULA is accepted." }
  New-Item -Path $eulaKey -Force | Out-Null
  New-ItemProperty -Path $eulaKey -Name FIRSTRUN -PropertyType DWord -Value 1 -Force | Out-Null
  Say "  EULA accepted."
}
$registered = $false
try { $registered = [bool](Get-ItemProperty $pidKey -ErrorAction Stop).PID } catch {}
if ($registered) { Say "  Product key already registered (skipping)." }
else {
  # Under -Yes with no -Key, Microsoft's own key dialog asks for it instead.
  while (-not ($Auto -and -not $Key) -and $Key.Trim() -notmatch '^[0-9A-Za-z]{5}(-[0-9A-Za-z]{5}){4}$') {
    if ($Key) { Say "  That is not a product key: five groups of five, XXXXX-XXXXX-XXXXX-XXXXX-XXXXX." 'Yellow' }
    $Key = (Ask-Text "  Your product key, from the disc case (XXXXX-XXXXX-XXXXX-XXXXX-XXXXX)" "-Key is not a product key: five groups of five, XXXXX-XXXXX-XXXXX-XXXXX-XXXXX.").Trim()
  }
  $reg = @('powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $Root 'tools\register_key.ps1'), '-Game', $Inst)
  if ($Key) { $reg += @('-Key', $Key.Trim()); Say "  Registering it with the game's own key check (Windows asks for permission once)..." }
  else { Say "  Type the product key from your disc case into Microsoft's key dialog (Windows asks for permission first)..." }
  $code = Exec $reg
  try { $registered = [bool](Get-ItemProperty $pidKey -ErrorAction Stop).PID } catch {}
  if (-not $registered) { Fail "the product key was not registered (exit code $code). Check it and try again." }
  Say "  Product key registered." 'Green'
}

# ---------------------------------------------------------------- analyze
$W = '_work\relift'
New-Item -ItemType Directory -Force $W | Out-Null
Step 5 "Finding the C++ vtables the binary does name (seconds)"
if ((Test-Path "$W\rtti_seeds.json") -and -not $Force) { Say "  Already done (skipping)." }
else { Run "RTTI" ($pyargs + @("$T\cpp\rtti.py", '_work\game\legends.exe', '-o', "$W\rtti.json", '--seeds', "$W\rtti_seeds.json")) }

# ---------------------------------------------------------------- catalog
Step 6 "Finding every function (the long one: about half an hour, once)"
if ((Test-Path "$W\functions.json") -and -not $Force) { Say "  Already done (skipping)." }
else { Run "Disassembling" ($pyargs + @('-u', "$T\disasm\disasm32.py", '_work\game\legends.exe', '-o', "$W\functions.json", '--seed-functions', "$W\rtti_seeds.json")) }

# ---------------------------------------------------------------- lift
Step 7 "Lifting every function to C (a few minutes)"
if ((Test-Path "$W\lift_stats.json") -and (Test-Path 'src\recomp\gen\recomp_dispatch.c') -and -not $Force) { Say "  Already done (skipping)." }
else { $env:PCRECOMP = $Toolkit; Run "Lifting" ($pyargs + @('run_lift.py', '--all', '--split', '150')) }

# ---------------------------------------------------------------- build
Step 8 "Building build\rol.exe (an hour or more, and most of your memory)"
$built = $false
if ((Test-Path 'build\rol.exe') -and -not $Force) { Say "  build\rol.exe is already there (skipping)."; $built = $true }
elseif (Ask "  Build it now? It runs at low priority, but expect a slow machine until it is done.") {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  function Find-VS { if (Test-Path $vswhere) { & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath } }
  $vs = Find-VS
  if (-not $vs) {
    Say "  Visual Studio 2022 with the C++ tools is not installed."
    if (-not (Ask "  Install the Visual Studio 2022 Build Tools with the C++ workload now (winget, about 3 GB, asks for admin)?")) {
      Fail "Visual Studio 2022 (or its Build Tools) with 'Desktop development with C++' is needed to build. Install it, then run Setup.cmd again."
    }
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail "winget is missing. Install 'App Installer' from the Microsoft Store, or the Visual Studio Build Tools yourself." }
    Exec @('winget', 'install', '-e', '--id', 'Microsoft.VisualStudio.2022.BuildTools', '--accept-package-agreements', '--accept-source-agreements',
           '--override', '--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended') | Out-Null
    $vs = Find-VS
    if (-not $vs) { Fail "the Visual Studio Build Tools did not install. Install them with 'Desktop development with C++', then run Setup.cmd again." }
  }
  Say "  Visual Studio: $vs"
  # build.cmd gets CMake and Ninja from vcvarsall when VS has its own copy.
  if (-not (Test-Path (Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'))) {
    foreach ($tool in @(@('cmake', 'Kitware.CMake'), @('ninja', 'Ninja-build.Ninja'))) {
      if (Get-Command $tool[0] -ErrorAction SilentlyContinue) { continue }
      if (-not (Ask "  $($tool[0]) is not installed. Install it now (winget)?")) { Fail "$($tool[0]) is required to build." }
      if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail "winget is missing. Install 'App Installer' from the Microsoft Store, or $($tool[0]) yourself." }
      Exec @('winget', 'install', '-e', '--id', $tool[1], '--accept-package-agreements', '--accept-source-agreements') | Out-Null
      Refresh-Path
      if (-not (Get-Command $tool[0] -ErrorAction SilentlyContinue)) { Fail "$($tool[0]) installed, but Windows has not picked it up yet: close this window and run Setup.cmd again." }
    }
  }
  # Each lifted chunk takes 1-3 GB in cl: one job per 4 cores and per 4 GB of
  # memory keeps it in RAM (12 jobs ran out on this project's 64 GB machine).
  $gb = [int]((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
  $j = [Math]::Max(1, [Math]::Min([int]([Environment]::ProcessorCount / 4), [int]($gb / 4)))
  $env:CMAKE_ARGS = "-DPCRECOMP=$Toolkit"
  Run "Building with $j jobs ($gb GB of memory)" @('cmd', '/c', 'build.cmd', '-j', "$j")
  if (-not (Test-Path 'build\rol.exe')) { Fail "the build finished without build\rol.exe." }
  $built = $true
}
if ($built) {
  Say "  Running the conformance check (a minute)..."
  Exec ($pyargs + @('tools\conformance.py')) | Out-Null
  Get-Content $Log -Tail 12 | Where-Object { $_ -match 'milestones|\[x\]|\[ \]|lift:|REGRESSION' } | ForEach-Object { Write-Host "  $_" }
}

# ---------------------------------------------------------------- zoom data
# The F10 menu's camera zoom swaps in rewritten copies of two archives; the
# host looks for them in rol_data\ next to rol.exe. The install is untouched.
Step 9 "Making the zoom archives for the F10 menu (a minute)"
if (-not $built) { Say "  No build\rol.exe yet (skipping)." }
elseif ((Test-Path 'build\rol_data\farthest') -and -not $Force) { Say "  Already done (skipping)." }
else { Run "Rewriting the camera and fog" ($pyargs + @('tools\make_enhanced_data.py', '_work\game', 'build\rol_data')) }

# ---------------------------------------------------------------- shortcut
Step 10 "Making the shortcut"
$lnk = Join-Path $Root 'Rise of Legends (recomp).cmd'
if (-not $built) { Say "  No build\rol.exe yet (skipping)." }
else {
  # rol.exe's --game and --exe default to _work\game, relative to the repo root.
  Set-Content -Path $lnk -Encoding ASCII -Value @(
    '@echo off',
    'rem Plays the recompiled game (made by tools\setup.ps1).',
    'cd /d "%~dp0"',
    'start "" build\rol.exe --run')
  Say "  $lnk"
}

Write-Host ""
if ($built) { Say "Done. Double-click 'Rise of Legends (recomp).cmd' to play." 'Green' }
else { Say "Done up to the build: run Setup.cmd again and say yes to build it." 'Green' }
Pause-End
