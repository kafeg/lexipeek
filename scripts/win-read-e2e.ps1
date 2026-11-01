# Read aloud end to end in a signed-in desktop session (not over SSH): a word's card, ▶ (its phases in its own name: the
# model loading, the reading being made; ▶ again after it), Ctrl+. again from the kept reading, a Russian word's card in study mode and a translated word's own ▶.
# The data dir: packs with de-ru and ru-de, the voice model in tts\, settings.json with "answer": "ru", "ui_lang": "en",
# "study": {"ru": ["de"]}, "speech": {"on": true}. Screenshots into the out dir, "ok|FAIL" lines on stdout.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\win-read-e2e.ps1 -Exe <LexiPeek.exe> -Data <data dir> -Out <dir>
param([Parameter(Mandatory)] [string]$Exe, [Parameter(Mandatory)] [string]$Data, [Parameter(Mandatory)] [string]$Out)
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
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
'@
$ae = [System.Windows.Automation.AutomationElement]
function All($proc) {
  $cond = New-Object System.Windows.Automation.PropertyCondition $ae::ProcessIdProperty, $proc.Id
  $out = @()
  foreach ($w in $ae::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    $out += $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
  }
  $out
}
function Texts($proc) { (All $proc | ForEach-Object { $_.Current.Name }) -join ' | ' }
function Buttons($proc, $name) { @(All $proc | Where-Object { $_.Current.Name -eq $name -and $_.Current.ControlType.ProgrammaticName -eq 'ControlType.Button' }) }
function Press($e) { $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
function Keys($k) { [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 300 }
function WinShift($letter) {
  $vk = [byte][char]$letter.ToUpper()
  foreach ($k in 0x5B, 0x10, $vk) { [Win]::keybd_event([byte]$k, 0, 0, [UIntPtr]::Zero) }
  foreach ($k in $vk, 0x10, 0x5B) { [Win]::keybd_event([byte]$k, 0, 2, [UIntPtr]::Zero) }
  Start-Sleep -Milliseconds 300
}
function CtrlDot { [Win]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero); [Win]::keybd_event(0xBE, 0, 0, [UIntPtr]::Zero); [Win]::keybd_event(0xBE, 0, 2, [UIntPtr]::Zero); [Win]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 300 }
function ClickIn($proc) {
  $proc.Refresh()
  $r = New-Object Win+Rect
  [void][Win]::GetWindowRect($proc.MainWindowHandle, [ref]$r)
  [void][Win]::SetForegroundWindow($proc.MainWindowHandle)
  [void][Win]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $r.B) / 2))
  [Win]::mouse_event(2, 0, 0, 0, 0); [Win]::mouse_event(4, 0, 0, 0, 0)
  Start-Sleep -Milliseconds 500
}
function Wavs { @(Get-ChildItem (Join-Path $Data 'readings') -Filter *.wav -ErrorAction SilentlyContinue).Count }
$play = 'Read aloud (Ctrl+.)'; $loadingName = 'Loading the voice model…'; $makingName = 'Making the reading…'

Get-Process LexiPeek -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item -Recurse -Force (Join-Path $Data 'readings') -ErrorAction SilentlyContinue
$app = Start-Process $Exe -ArgumentList '--data', $Data -PassThru
Start-Sleep 5
Check (-not $app.HasExited) 'the app runs'

$file = Join-Path $env:TEMP 'lexipeek-read.txt'
Set-Content $file 'Haus' -Encoding UTF8
$pad = Start-Process notepad $file -PassThru
Start-Sleep 2
ClickIn $pad
Keys '^a'
WinShift 'd'
Start-Sleep 4
Shot 'r1-card'
$b = Buttons $app $play
Check ($b.Count -ge 1) "the card has ▶ ($((Texts $app).Substring(0, 120)))"
if ($b.Count) {
  $t0 = Get-Date
  Press $b[0]
  $loading = $false; $making = $false
  $rows = @(All $app).Count
  while (((Get-Date) - $t0).TotalSeconds -lt 120 -and (Wavs) -eq 0) {
    if ((Buttons $app $loadingName).Count) { if (-not $loading) { Shot 'r2-loading' }; $loading = $true }
    if ((Buttons $app $makingName).Count) { if (-not $making) { Shot 'r2-making' }; $making = $true }
    Start-Sleep -Milliseconds 300
  }
  $made = ((Get-Date) - $t0).TotalSeconds
  Check ((Wavs) -eq 1) "▶ made a reading in $([int]$made) s"
  Check $loading '▶ showed the model loading'
  Check $making '▶ showed the reading being made'
  Check (-not ((Texts $app) -match 'Loading the voice model')) 'no line about the model in the body'
  Start-Sleep 4
  Shot 'r3-done'
  Check ((Buttons $app $play).Count -ge 1 -and (Buttons $app $stop).Count -eq 0) 'the button is ▶ again after the reading'
  $t1 = Get-Date
  CtrlDot
  Start-Sleep 2
  Check (-not $app.HasExited) 'Ctrl+. reads again'
  Check ((Wavs) -eq 1) 'read again from the kept reading (no new WAV)'
}
Keys '{ESC}'
Start-Sleep 1

# A Russian word in study mode (Russian answer, German learnt): a translated word's own ▶.
Set-Content $file 'дом' -Encoding UTF8
Stop-Process $pad -Force
$pad = Start-Process notepad $file -PassThru
Start-Sleep 2
ClickIn $pad
Keys '^a'
WinShift 'd'
Start-Sleep 4
Shot 'r4-study'
$b = Buttons $app $play
Check ($b.Count -ge 3) "study mode: the bottom ▶ and the translated words' ▶ ($($b.Count))"
if ($b.Count -ge 3) {
  $before = Wavs
  Press $b[1]
  $t0 = Get-Date
  while (((Get-Date) - $t0).TotalSeconds -lt 60 -and (Wavs) -eq $before) { Start-Sleep -Milliseconds 300 }
  Check ((Wavs) -eq $before + 1) "a translated word's ▶ made its reading in $([int]((Get-Date) - $t0).TotalSeconds) s"
  Start-Sleep 3
  Shot 'r5-study-read'
}
Keys '{ESC}'
Stop-Process $pad -Force
Stop-Process $app -Force
Remove-Item $file
"failed: $script:failed"
exit $script:failed
