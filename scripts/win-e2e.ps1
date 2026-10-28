# The Windows app end to end, in a signed-in desktop session (not over SSH: no desktop there): LexiPeek.exe started on
# a data dir with packs, a word selected in Notepad, the lookup hotkey, the card read by UI Automation, Esc, the input
# hotkey, a lexipeek:// link, a second launch; a screenshot after each step into the out dir, "ok|FAIL" lines on stdout.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\win-e2e.ps1 -Exe <LexiPeek.exe> -Data <data dir> -Out <dir>
param([Parameter(Mandatory)] [string]$Exe, [Parameter(Mandatory)] [string]$Data, [Parameter(Mandatory)] [string]$Out)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing, UIAutomationClient, UIAutomationTypes, Microsoft.VisualBasic
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
# The texts of the app's visible top-level windows (the card is one).
function AppTexts($proc) {
  $root = [System.Windows.Automation.AutomationElement]::RootElement
  $cond = New-Object System.Windows.Automation.PropertyCondition ([System.Windows.Automation.AutomationElement]::ProcessIdProperty), $proc.Id
  $texts = @()
  foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    foreach ($e in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
      $texts += $e.Current.Name
    }
  }
  $texts -join ' | '
}
function Keys($k) { [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 300 }
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
'@
# A real click in the middle of a window: the focus is there (a notification may hold it otherwise).
function ClickIn($proc) {
  $proc.Refresh()
  $r = New-Object Win+Rect
  [void][Win]::GetWindowRect($proc.MainWindowHandle, [ref]$r)
  [void][Win]::SetForegroundWindow($proc.MainWindowHandle)
  [void][Win]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $r.B) / 2))
  [Win]::mouse_event(2, 0, 0, 0, 0); [Win]::mouse_event(4, 0, 0, 0, 0)
  Start-Sleep -Milliseconds 500
}

# The OS parts first (in a desktop session the Credential Manager and hotkeys work, unlike over SSH).
$selfTest = Join-Path $Out 'self-test.txt'
Start-Process $Exe -ArgumentList '--self-test', $selfTest -Wait
Get-Content $selfTest
$script:failed += @(Get-Content $selfTest | Where-Object { $_ -like 'FAIL*' }).Count

Get-Process LexiPeek -ErrorAction SilentlyContinue | Stop-Process -Force
$app = Start-Process $Exe -ArgumentList '--data', $Data -PassThru
Start-Sleep 4
Check (-not $app.HasExited) 'the app runs (tray)'

$file = Join-Path $env:TEMP 'lexipeek-e2e.txt'
Set-Content $file 'Haus steht am Ende der Strasse.' -Encoding UTF8
$pad = Start-Process notepad $file -PassThru
Start-Sleep 2
ClickIn $pad
Keys '^{HOME}'
Keys '^+{RIGHT}'
Shot '1-selected'

Keys '%d'
Start-Sleep 4
Shot '2-card'
$t = AppTexts $app
Check ($t -match 'Haus') "the card for the selection: $($t.Substring(0, [Math]::Min(200, $t.Length)))"

Keys '{ESC}'
Start-Sleep 1
Shot '3-closed'
Check (-not ((AppTexts $app) -match 'Haus')) 'Esc closes the card'

ClickIn $pad
Keys '%a'
Start-Sleep 2
Keys 'Tag~'
Start-Sleep 4
Shot '4-typed'
Check ((AppTexts $app) -match 'Tag') 'the input hotkey: a card to type in, Return looks it up'
Keys '{ESC}'

Start-Process 'lexipeek://lookup?text=Stra%C3%9Fe'
Start-Sleep 4
Shot '5-link'
Check ((AppTexts $app) -match 'Stra.e') 'a lexipeek:// link opens its card'  # ASCII: PowerShell 5 reads a BOM-less script as ANSI
Keys '{ESC}'

$second = Start-Process $Exe -ArgumentList '--data', $Data -PassThru
Start-Sleep 3
Check ($second.HasExited) 'a second launch hands over and quits'
Shot '6-window'
Check ((AppTexts $app) -match 'Help') 'the second launch opens the LexiPeek window'

Stop-Process $pad -Force
Stop-Process $app -Force
Remove-Item $file
"failed: $script:failed"
exit $script:failed
