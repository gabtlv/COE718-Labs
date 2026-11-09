# Laptop test build of MediaCenter (and optional flash).
#   .\build.ps1          build only
#   .\build.ps1 -Flash   build, then download to the board through the ULINK
# The free Keil on the laptop can only link 32 KB, so this script switches
# config.h to LITE_BUILD 1 (tiny stand-in photo), builds (and flashes),
# then puts it back to 0. config.h therefore always stays on the full build
# for the lab PC.
param([switch]$Flash)
$d = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $d
$proj = if (Test-Path MediaCenter.uvprojx) { 'MediaCenter.uvprojx' } else { 'MediaCenter.uvproj' }
$cfg = Join-Path $d 'config.h'
$orig = [IO.File]::ReadAllText($cfg)
[IO.File]::WriteAllText($cfg, ($orig -replace '#define LITE_BUILD\s+0', '#define LITE_BUILD  1'))
try {
  Start-Process C:\Keil_v5\UV4\UV4.exe -ArgumentList "-r $proj -j0 -o build_log.txt" -Wait
  Get-Content build_log.txt
  if (Test-Path Lst\MediaCenter.map) { Select-String -Path Lst\MediaCenter.map -Pattern "Total ROM Size" | % Line }
  if ($Flash) {
    Start-Process C:\Keil_v5\UV4\UV4.exe -ArgumentList "-f $proj -j0 -o flash_log.txt" -Wait
    Get-Content flash_log.txt
  }
} finally {
  [IO.File]::WriteAllText($cfg, $orig)
}
