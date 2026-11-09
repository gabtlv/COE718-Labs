# Laptop test build of MediaCenter.
# The free Keil on the laptop can only link 32 KB, so this script switches
# config.h to LITE_BUILD 1 (tiny stand-in photo), builds, then puts it back
# to 0. config.h therefore always stays on the full build for the lab PC.
$d = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $d
$cfg = Join-Path $d 'config.h'
$orig = [IO.File]::ReadAllText($cfg)
[IO.File]::WriteAllText($cfg, ($orig -replace '#define LITE_BUILD\s+0', '#define LITE_BUILD  1'))
try {
  Start-Process C:\Keil_v5\UV4\UV4.exe -ArgumentList '-r MediaCenter.uvproj -j0 -o build_log.txt' -Wait
} finally {
  [IO.File]::WriteAllText($cfg, $orig)
}
Get-Content build_log.txt
if (Test-Path Lst\MediaCenter.map) { Select-String -Path Lst\MediaCenter.map -Pattern "Total ROM Size" | % Line }
