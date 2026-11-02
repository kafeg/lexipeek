# The Windows installer end to end, in a signed-in desktop session, on a machine without LexiPeek: LexiPeek-Windows-Setup.exe
# run as a person runs it (its last page's Finish: LexiPeek starts, its first run opens), what it put where (the folder,
# the Start menu, one entry in Settings -> Apps), again over the running app (it gives way), then its uninstaller (all of it
# gone, the data dir kept). A screenshot after each step into the out dir, "ok|FAIL" lines on stdout.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\win-install-e2e.ps1 -Setup <LexiPeek-Windows-Setup.exe> -Out <dir>
param([Parameter(Mandatory)] [string]$Setup, [Parameter(Mandatory)] [string]$Out)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing, UIAutomationClient, UIAutomationTypes
New-Item -ItemType Directory -Force $Out | Out-Null
$script:failed = 0
function Check($ok, $what) { if ($ok) { "ok   $what" } else { "FAIL $what"; $script:failed++ } }
function Shot($name) {
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  $bmp.Save((Join-Path $Out "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}
function Until($seconds, $test) { for ($i = 0; $i -lt $seconds * 2; $i++) { $r = & $test; if ($r) { return $r }; Start-Sleep -Milliseconds 500 }; & $test }
# A top-level window of the process whose title has the text.
function Window($proc, $title) {
  $ae = [System.Windows.Automation.AutomationElement]
  $ae::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children,
    (New-Object System.Windows.Automation.PropertyCondition $ae::ProcessIdProperty, $proc.Id)) | Where-Object { $_.Current.Name -match $title } | Select-Object -First 1
}
$dir = Join-Path $env:LOCALAPPDATA 'Programs\LexiPeek'
$exe = Join-Path $dir 'LexiPeek.exe'
$link = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\LexiPeek.lnk'
$apps = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
$running = { @(Get-Process LexiPeek -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }).Count -gt 0 }
Check (-not (Test-Path $dir)) "no LexiPeek installed before ($dir)"

# As a person: Install on its one page, then Finish on its last with "Launch LexiPeek" ticked. Its window is the
# wizard's own process (Setup.tmp), any LexiPeek-Windows-Setup* one.
$p = Start-Process $Setup -PassThru
$button = { param($name)
  foreach ($s in Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'LexiPeek-Windows-Setup*' }) {
    $w = Window $s 'LexiPeek'
    if ($w) { $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) |
      Where-Object { $_.Current.Name -replace '&' -eq $name } |  # Inno's buttons are panes to UI Automation
      Select-Object -First 1 }
  }
}
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Mouse {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e);
}
'@
function Press($e) {
  $r = $e.Current.BoundingRectangle
  [void][Mouse]::SetCursorPos([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2))
  [Mouse]::mouse_event(2, 0, 0, 0, 0); [Mouse]::mouse_event(4, 0, 0, 0, 0)
}
$i = Until 60 { & $button 'Install' }
Check $i 'the installer asks once: Install'
if ($i) { Press $i }
$b = Until 120 { & $button 'Finish' }
Check ($b -and (Test-Path $exe)) 'it puts LexiPeek in place and shows its last page'
Shot '1-finished'
if ($b) { Press $b }
Check (Until 30 { $p.HasExited }) 'Finish ends the installer'
Check (Until 30 $running) 'LexiPeek starts from its folder'
$app = Get-Process LexiPeek | Where-Object { $_.Path -eq $exe } | Select-Object -First 1
Check (Until 20 { Window $app 'Welcome' }) 'its first run opens'
Shot '2-first-run'
Check (Test-Path $link) 'a Start menu entry'
Check ((Test-Path "$apps\LexiPeek_is1") -and -not (Test-Path "$apps\LexiPeek")) 'one entry in Settings -> Apps (the installer''s)'
Check (Test-Path (Join-Path $dir 'THIRD_PARTY_NOTICES.txt')) 'the notices beside it'

# Again over the running app (an update): it gives way, the installer ends well.
$p = Start-Process $Setup -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES' -PassThru -Wait
Check ($p.ExitCode -eq 0 -and -not (& $running)) "installed again over the running app (exit $($p.ExitCode); it quit)"

# Its uninstaller: the folder, the Start menu entry and the Apps entry gone; the data dir stays.
Start-Process $exe
Check (Until 20 $running) 'started again'
$data = Join-Path $env:LOCALAPPDATA 'LexiPeek'
$u = Start-Process (Join-Path $dir 'unins000.exe') -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES' -PassThru -Wait
Check ($u.ExitCode -eq 0) "the uninstaller ends well (exit $($u.ExitCode))"
Check (Until 20 { -not (Test-Path $dir) }) 'its folder is gone'
Check (-not (& $running)) 'LexiPeek is not running'
Check (-not (Test-Path $link)) 'the Start menu entry is gone'
Check (-not (Test-Path "$apps\LexiPeek_is1")) 'the Apps entry is gone'
Check (Test-Path $data) 'the data dir stays'
Shot '3-uninstalled'
"failed: $script:failed"
exit $script:failed
