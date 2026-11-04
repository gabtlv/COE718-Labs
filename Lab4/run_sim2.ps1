param([string]$proj, [string]$ini, [int]$maxSec = 300)
$dir = Split-Path $proj
$opt = [IO.Path]::ChangeExtension($proj, '.uvoptx')
$orig = [IO.File]::ReadAllText($opt)
[IO.File]::WriteAllText($opt, $orig.Replace('<sIfile></sIfile>', "<sIfile>$ini</sIfile>"), (New-Object Text.UTF8Encoding $false))
$log = "$dir\sim_log.txt"; Remove-Item $log -ErrorAction SilentlyContinue
$p = Start-Process C:\Keil_v5\UV4\UV4.exe -ArgumentList "-d `"$proj`"" -PassThru
$t = 0
while ($t -lt $maxSec) { Start-Sleep 3; $t += 3; if ((Test-Path $log) -and (Select-String $log -Pattern 'END_OF_CHECK' -Quiet)) { break } }
Start-Sleep 1; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep 1
[IO.File]::WriteAllText($opt, $orig, (New-Object Text.UTF8Encoding $false))
"(sim took ~$t s)"
Get-Content $log -ErrorAction SilentlyContinue | Select-String -NotMatch '^\s*$|stopper|^G$|END_OF' | % Line
