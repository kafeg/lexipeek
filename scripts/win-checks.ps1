# CI's checks of the WinUI shell on a signed-in desktop: --self-test (the OS parts), then --snapshots of <Out>\cases.txt
# on the data dir (snapshot-cases.sh's card cases, light and dark). Fails on a failed check or a case not drawn.
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$Data, [Parameter(Mandatory)][string]$Out)
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path $Exe).Path
$Data = (Resolve-Path $Data).Path
$Out = (Resolve-Path $Out).Path
function Run([string]$What, [string[]]$Argv) {
  $t = Get-Date
  $p = Start-Process $Exe -ArgumentList $Argv -PassThru
  if (-not $p.WaitForExit(900000)) { $p.Kill(); throw "${What}: did not finish in 15 min" }
  Write-Host "${What}: exit $($p.ExitCode), $([int]((Get-Date) - $t).TotalSeconds) s"
  return $p.ExitCode
}
$code = Run 'self-test' @('--self-test', "$Out\self-test.txt")
Get-Content "$Out\self-test.txt"
if ($code -ne 0) { throw "self-test: $($code) failed" }
$code = Run 'snapshots' @('--data', $Data, '--snapshots', "$Out\snapshots", '--cases', "$Out\cases.txt")
if (Test-Path "$Out\snapshots\failed.txt") { Get-Content "$Out\snapshots\failed.txt" }
"$((Get-ChildItem "$Out\snapshots" -Filter *.png).Count) images"
if ($code -ne 0) { throw "snapshots: $($code) failed" }
