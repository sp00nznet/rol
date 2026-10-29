# Register your copy of Rise of Legends: run the game's own mgspid.dll!GetPidx,
# with the arguments legends.exe passes, so its CD-key dialog asks for the key
# on your disc case and writes the product ID the way the installer would.
#
# Why: tools\setup.ps1 assembles the install from the discs without running the
# MSI, and the MSI (an InstallScript program) is what normally calls this. The
# game calls GetPidx on every start and quits cleanly, with no message, when it
# fails (docs/host.md). Nothing here bypasses the check; it is the check.
#
# Needs a 32-bit process (the DLL is 32-bit) with admin rights (it writes
# HKLM\SOFTWARE\Microsoft\Microsoft Games\Rise of Legends, which a 32-bit
# process sees under WOW6432Node, as the 32-bit game does). This script
# relaunches itself that way; Windows asks once for permission.
param([string]$Game = "")
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Game) { $Game = Join-Path $Root '_work\game' }
$Dll = Join-Path $Game 'mgspid.dll'
if (-not (Test-Path $Dll)) { Write-Host "No mgspid.dll in $Game." -ForegroundColor Red; exit 1 }

$wow = "$env:WINDIR\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
  [Security.Principal.WindowsBuiltInRole]::Administrator)
if ([Environment]::Is64BitProcess -or -not $admin) {
  $p = Start-Process $wow -Verb RunAs -Wait -PassThru -ArgumentList @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Game', "`"$Game`"")
  exit $p.ExitCode
}

Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices; using System.Text;
public static class MgsPid {
  [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern bool SetDllDirectory(string dir);
  [DllImport("mgspid.dll", EntryPoint = "GetPidx", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
  public static extern int GetPidx(IntPtr root, string key, string product, StringBuilder pid);
}
"@
[MgsPid]::SetDllDirectory($Game) | Out-Null
$pid_ = New-Object System.Text.StringBuilder 256
$hklm = [IntPtr]0x80000002   # HKEY_LOCAL_MACHINE, as legends.exe passes it
$ok = [MgsPid]::GetPidx($hklm, 'SOFTWARE\Microsoft\Microsoft Games\Rise of Legends', 'Rise of Legends', $pid_)
if ($ok -eq 1) { Write-Host "Registered. Product ID: $pid_" -ForegroundColor Green; Start-Sleep 3; exit 0 }
Write-Host "Not registered (GetPidx returned $ok)." -ForegroundColor Yellow; Start-Sleep 5; exit 2
