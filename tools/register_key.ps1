# Register your copy of Rise of Legends: run the game's own mgspid.dll!GetPidx
# with the arguments legends.exe passes, so its CD-key check stores the product
# ID the way the installer would.
#
#   powershell -File tools\register_key.ps1 -Key XXXXX-XXXXX-XXXXX-XXXXX-XXXXX
#   powershell -File tools\register_key.ps1          # type it into the dialog
#
# Why: tools\setup.ps1 assembles the install from the discs without running the
# MSI, and the MSI (an InstallScript program) is what normally does this. The
# game calls GetPidx on every start and quits cleanly, with no message, when it
# fails (docs/host.md). Nothing is patched or bypassed: this is the game's check.
#
# With -Key, GetPidx still shows Microsoft's key dialog; a helper thread types
# the five groups into its boxes (IDs 1006-1010) and presses OK, so all of
# mgspid's own validation (pidgen.dll!PIDGenSimpA) and storage runs unchanged.
# If mgspid rejects the key, its message box is read, closed and reported.
#
# Needs a 32-bit process (the DLL is 32-bit) with admin rights (it writes
# HKLM\SOFTWARE\Microsoft\Microsoft Games\Rise of Legends, which a 32-bit
# process sees under WOW6432Node, as the 32-bit game does). This script
# relaunches itself that way; Windows asks once for permission.
param([string]$Game = "", [string]$Key = "")
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Game) { $Game = Join-Path $Root '_work\game' }
$Dll = Join-Path $Game 'mgspid.dll'
if (-not (Test-Path $Dll)) { Write-Host "No mgspid.dll in $Game." -ForegroundColor Red; exit 1 }
$Key = $Key.Trim().ToUpper()
if ($Key -and $Key -notmatch '^[0-9A-Z]{5}(-[0-9A-Z]{5}){4}$') {
  Write-Host "A product key is five groups of five letters and digits: XXXXX-XXXXX-XXXXX-XXXXX-XXXXX." -ForegroundColor Red
  exit 1
}

$wow = "$env:WINDIR\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
  [Security.Principal.WindowsBuiltInRole]::Administrator)
if ([Environment]::Is64BitProcess -or -not $admin) {
  $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Game', "`"$Game`"")
  if ($Key) { $a += @('-Key', $Key) }
  $p = Start-Process $wow -Verb RunAs -Wait -PassThru -ArgumentList $a
  exit $p.ExitCode
}

Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices; using System.Text; using System.Threading;
public static class MgsPid {
  [DllImport("kernel32", CharSet = CharSet.Unicode)] public static extern bool SetDllDirectory(string dir);
  [DllImport("mgspid.dll", EntryPoint = "GetPidx", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
  public static extern int GetPidx(IntPtr root, string key, string product, StringBuilder pid);

  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32")] static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32", CharSet = CharSet.Ansi)] static extern bool SetDlgItemTextA(IntPtr h, int id, string s);
  [DllImport("user32", CharSet = CharSet.Ansi)] static extern int GetClassNameA(IntPtr h, StringBuilder s, int n);
  [DllImport("user32")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32", CharSet = CharSet.Unicode)] static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t);

  public static string Key = "", Refusal = "";
  static readonly uint Me = (uint)System.Diagnostics.Process.GetCurrentProcess().Id;

  // Visible dialogs (#32770) of this process, newest state each call.
  static IntPtr[] Dialogs() {
    var found = new System.Collections.Generic.List<IntPtr>();
    EnumWindows((h, l) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      var c = new StringBuilder(32); GetClassNameA(h, c, 32);
      if (pid == Me && c.ToString() == "#32770" && IsWindowVisible(h)) found.Add(h);
      return true; }, IntPtr.Zero);
    return found.ToArray();
  }

  // Type the key into the dialog's five boxes and press OK; then, if mgspid
  // answers with a message box (an invalid key), record its text and close it.
  public static void Start() { var t = new Thread(Fill); t.IsBackground = true; t.Start(); }

  public static void Fill() {
    string[] g = Key.Split('-');
    for (int t = 0; t < 600; t++, Thread.Sleep(50)) {
      foreach (IntPtr h in Dialogs()) {
        if (GetDlgItem(h, 1006) == IntPtr.Zero) continue;
        for (int i = 0; i < 5; i++) SetDlgItemTextA(h, 1006 + i, g[i]);
        Thread.Sleep(200);
        PostMessage(h, 0x0111, (IntPtr)1, GetDlgItem(h, 1));   // WM_COMMAND IDOK
        for (int u = 0; u < 100; u++, Thread.Sleep(50)) {
          foreach (IntPtr m in Dialogs()) {
            if (GetDlgItem(m, 1006) != IntPtr.Zero) continue;
            var s = new StringBuilder(512);
            IntPtr txt = FindWindowExW(m, IntPtr.Zero, "Static", null);
            while (txt != IntPtr.Zero && s.Length == 0) {
              GetWindowTextW(txt, s, 512);
              txt = FindWindowExW(m, txt, "Static", null);
            }
            Refusal = s.Length > 0 ? s.ToString() : "a message box";
            PostMessage(m, 0x0010, IntPtr.Zero, IntPtr.Zero);      // WM_CLOSE
            Thread.Sleep(300);
            foreach (IntPtr d in Dialogs())                          // the key dialog again
              if (GetDlgItem(d, 1006) != IntPtr.Zero) PostMessage(d, 0x0111, (IntPtr)2, IntPtr.Zero);   // IDCANCEL
            return;
          }
        }
        return;
      }
    }
  }
}
"@
[MgsPid]::SetDllDirectory($Game) | Out-Null
if ($Key) {
  [MgsPid]::Key = $Key
  [MgsPid]::Start()
}
$out = New-Object System.Text.StringBuilder 256
$hklm = [IntPtr]0x80000002   # HKEY_LOCAL_MACHINE, as legends.exe passes it
$ok = [MgsPid]::GetPidx($hklm, 'SOFTWARE\Microsoft\Microsoft Games\Rise of Legends', 'Rise of Legends', $out)
if ($ok -eq 1) { Write-Host "Registered. Product ID: $out" -ForegroundColor Green; Start-Sleep 3; exit 0 }
if ([MgsPid]::Refusal) { Write-Host "The key was refused: $([MgsPid]::Refusal)" -ForegroundColor Yellow }
else { Write-Host "Not registered (GetPidx returned $ok)." -ForegroundColor Yellow }
Start-Sleep 5; exit 2
