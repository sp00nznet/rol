param([string]$Msi, [string]$OutDir)
$ErrorActionPreference = 'Stop'
$inst = New-Object -ComObject WindowsInstaller.Installer
$db = $inst.GetType().InvokeMember('OpenDatabase','InvokeMethod',$null,$inst,@($Msi,0))

function Dump($sql, $cols, $path) {
  $v = $db.GetType().InvokeMember('OpenView','InvokeMethod',$null,$db,@($sql))
  $v.GetType().InvokeMember('Execute','InvokeMethod',$null,$v,$null)
  $out = New-Object System.Collections.Generic.List[string]
  while ($true) {
    $r = $v.GetType().InvokeMember('Fetch','InvokeMethod',$null,$v,$null)
    if ($null -eq $r) { break }
    $f = @()
    for ($i=1; $i -le $cols; $i++) {
      $f += [string]$r.GetType().InvokeMember('StringData','GetProperty',$null,$r,@($i))
    }
    $out.Add(($f -join "`t"))
  }
  [System.IO.File]::WriteAllLines($path, $out)
  Write-Output "$path : $($out.Count) rows"
}

Dump 'SELECT `File`,`FileName`,`Component_`,`Sequence` FROM `File`' 4 "$OutDir\file.tsv"
Dump 'SELECT `Component`,`Directory_` FROM `Component`' 2 "$OutDir\component.tsv"
Dump 'SELECT `Directory`,`Directory_Parent`,`DefaultDir` FROM `Directory`' 3 "$OutDir\directory.tsv"
Dump 'SELECT `DiskId`,`LastSequence`,`Cabinet` FROM `Media`' 3 "$OutDir\media.tsv"
