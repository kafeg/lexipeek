# The lookup hotkey in other apps, in a signed-in desktop session: each opens "Haus steht am Ende der Strasse.", "Haus"
# is selected (a double click in a browser, the keyboard in an editor), the probe says what UI Automation sees, Alt+D
# opens the card; "ok|FAIL <app>" lines, a screenshot per app, the probe's lines into the out dir.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\win-apps.ps1 -Exe <LexiPeek.exe> -Data <data dir> -Out <dir>
param([Parameter(Mandatory)] [string]$Exe, [Parameter(Mandatory)] [string]$Data, [Parameter(Mandatory)] [string]$Out)
$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing, UIAutomationClient, UIAutomationTypes
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Win {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
'@
New-Item -ItemType Directory -Force $Out | Out-Null
function Shot($n) {
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  $bmp.Save((Join-Path $Out "$n.png")); $g.Dispose(); $bmp.Dispose()
}
function Keys($k) { [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 300 }
function Click($x, $y, $times) {
  [void][Win]::SetCursorPos($x, $y)
  for ($i = 0; $i -lt $times; $i++) { [Win]::mouse_event(2, 0, 0, 0, 0); [Win]::mouse_event(4, 0, 0, 0, 0); Start-Sleep -Milliseconds 80 }
}
function Texts($proc) {
  $c = New-Object System.Windows.Automation.PropertyCondition [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $proc.Id
  $out = @()
  foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c)) {
    foreach ($e in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) { $out += $e.Current.Name }
  }
  $out -join ' | '
}
# The element showing the text (a browser's paragraph), searched in every top-level window.
function FindText {
  $c = New-Object System.Windows.Automation.PropertyCondition [System.Windows.Automation.AutomationElement]::NameProperty, 'Haus steht am Ende der Strasse.'
  for ($i = 0; $i -lt 20; $i++) {
    $e = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $c)
    if ($e) { return $e }
    Start-Sleep -Milliseconds 500
  }
  $null
}

$page = Join-Path $Out 'haus.html'
Set-Content $page '<!doctype html><html><body style="font: 32px sans-serif; margin: 80px"><p>Haus steht am Ende der Strasse.</p></body></html>' -Encoding ASCII
$text = Join-Path $Out 'haus.txt'
Set-Content $text 'Haus steht am Ende der Strasse.' -Encoding ASCII
$url = 'file:///' + ($page -replace '\\', '/')
$apps = @(
  @{ name = 'notepad'; file = 'notepad.exe'; args = @($text); browser = $false; proc = 'notepad' },
  @{ name = 'wordpad'; file = 'C:\Program Files\Windows NT\Accessories\wordpad.exe'; args = @($text); browser = $false; proc = 'wordpad' },
  @{ name = 'edge'; file = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'; args = @('--no-first-run', "--user-data-dir=$Out\profiles\edge", $url); browser = $true; proc = 'msedge' },
  @{ name = 'chrome'; file = 'C:\Program Files\Google\Chrome\Application\chrome.exe'; args = @('--no-first-run', '--no-default-browser-check', "--user-data-dir=$Out\profiles\chrome", $url); browser = $true; proc = 'chrome' },
  @{ name = 'firefox'; file = 'C:\Program Files\Mozilla Firefox\firefox.exe'; args = @('-no-remote', '-profile', "$Out\profiles\firefox", $url); browser = $true; proc = 'firefox' }
)
New-Item -ItemType Directory -Force (Join-Path $Out 'profiles\firefox') | Out-Null

Get-Process LexiPeek -ErrorAction SilentlyContinue | Stop-Process -Force
$lp = Start-Process $Exe -ArgumentList '--data', $Data -PassThru
Start-Sleep 4
$failed = 0
foreach ($a in $apps) {
  if (-not (Get-Command $a.file -ErrorAction SilentlyContinue) -and -not (Test-Path $a.file)) { "skip $($a.name): not installed"; continue }
  Get-Process $a.proc -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep 1
  $p = Start-Process $a.file -ArgumentList $a.args -PassThru
  Start-Sleep 6
  if ($a.browser) {
    $e = FindText
    if (-not $e) { "FAIL $($a.name): the page's text not found"; $failed++; Shot $a.name; Get-Process $a.proc -ErrorAction SilentlyContinue | Stop-Process -Force; continue }
    $r = $e.Current.BoundingRectangle
    Click ([int]($r.X + 20)) ([int]($r.Y + $r.Height / 2)) 2  # a double click on "Haus"
  } else {
    $w = Get-Process $a.proc | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    [void][Win]::SetForegroundWindow($w.MainWindowHandle)
    Start-Sleep 1
    Keys '^{HOME}'; Keys '^+{RIGHT}'
  }
  Start-Sleep 1
  $probe = Join-Path $Out "$($a.name)-probe.txt"
  Start-Process $Exe -ArgumentList '--probe-selection', $probe -Wait
  Keys '%d'
  Start-Sleep 4
  Shot $a.name
  $t = Texts $lp
  if ($t -match 'Haus \(Hauses') { "ok   $($a.name)" } else { "FAIL $($a.name): $(($t -replace '\s+', ' ').Substring(0, [Math]::Min(160, $t.Length)))"; $failed++ }
  "     probe: " + ((Get-Content $probe) -match 'element\[0\]|text=|edit fallback|front app' -join ' / ')
  Keys '{ESC}'
  Get-Process $a.proc -ErrorAction SilentlyContinue | Stop-Process -Force
}
Stop-Process $lp -Force
"failed: $failed"
exit $failed
