; The Windows installer (Inno Setup 6): for this user, no admin, into the folder the app's own install uses
; (os::install: %LOCALAPPDATA%\Programs\LexiPeek), a Start menu entry, an uninstaller (Settings → Apps); LexiPeek starts
; at the end, so its first run opens. The language packs and the settings stay on uninstall (the data dir).
; Built by release.yml: ISCC /DVersion=0.2.N /DSource=<scripts\build-win.cmd's win\LexiPeek> /O<dir> win\LexiPeek.iss
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Source
  #define Source "..\build\win\LexiPeek"
#endif

[Setup]
AppId=LexiPeek
AppName=LexiPeek
AppVersion={#Version}
AppVerName=LexiPeek {#Version}
AppPublisher=LexiPeek
AppPublisherURL=https://github.com/kafeg/lexipeek
VersionInfoVersion={#Version}
DefaultDirName={localappdata}\Programs\LexiPeek
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
SetupIconFile=..\qt\icons\lexipeek.ico
UninstallDisplayIcon={app}\LexiPeek.exe
UninstallDisplayName=LexiPeek
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputBaseFilename=LexiPeek-Windows-Setup
ShowLanguageDialog=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "pt"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"

[Files]
Source: "{#Source}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\LexiPeek"; Filename: "{app}\LexiPeek.exe"; WorkingDir: "{app}"

[Registry]
; The app's own install (from the zip) had its own entry in Settings → Apps: one entry, this one.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\LexiPeek"; Flags: deletekey

[Run]
Filename: "{app}\LexiPeek.exe"; Description: "{cm:LaunchProgram,LexiPeek}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; A running LexiPeek quits; its link handler, autostart and Start menu entry go (the app's own uninstall, without asking).
Filename: "{app}\LexiPeek.exe"; Parameters: "--uninstall --yes"; RunOnceId: "LexiPeek"; Flags: waituntilterminated

[Code]
// A running LexiPeek gives way before its files are replaced: asked to quit (as a newer copy asks it), else ended.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code, I: Integer;
begin
  Result := '';
  if FindWindowByWindowName('LexiPeek receiver') = 0 then Exit;
  if FileExists(ExpandConstant('{app}\LexiPeek.exe')) then
    Exec(ExpandConstant('{app}\LexiPeek.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code);
  for I := 1 to 30 do
    if FindWindowByWindowName('LexiPeek receiver') <> 0 then Sleep(100);
  if FindWindowByWindowName('LexiPeek receiver') <> 0 then
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/im LexiPeek.exe /f', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;
