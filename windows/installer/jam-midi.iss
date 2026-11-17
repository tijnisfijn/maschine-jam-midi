; Inno Setup script for Maschine Jam MIDI (Windows).
; Build it with windows\installer\build-installer.ps1, which needs Inno Setup 6.
;
; The installer is per-user (PrivilegesRequired=lowest): it goes in
; %LOCALAPPDATA%\Programs\Maschine Jam MIDI and asks for no admin rights. That suits an unsigned
; build: there is no UAC prompt on top of the SmartScreen warning. See the note on signing in
; windows\README.md.

#define AppName "Maschine Jam MIDI"
; Keep this in step with the repository's tags: one version across both platforms.
#define AppVersion "1.2.0"
#define AppPublisher "Tijn"
#define AppURL "https://github.com/tijnisfijn/maschine-jam-midi"
#define AppExe "jam-midi.exe"

[Setup]
AppId={{8F3A5C21-7D4E-4B19-9A62-5E0C1F7B2D84}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\build\installer
; No spaces: GitHub turns them into dots on a release asset, and this matches the macOS zip.
OutputBaseFilename=maschine-jam-midi-{#AppVersion}-windows-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\..\LICENSE
InfoBeforeFile=before-install.txt
UninstallDisplayIcon={app}\{#AppExe}
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} driver

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startupicon"; Description: "Start {#AppName} when I log on"; GroupDescription: "Extra:"

[Files]
Source: "..\build\jam-midi.exe";   DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\jam-send.exe";   DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\strip-test.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md";            DestDir: "{app}"; DestName: "README-windows.md"; Flags: ignoreversion
Source: "..\..\MIDI-MAP.md";       DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\PROTOCOL.md";       DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE";           DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\NOTICE";            DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autostartup}\{#AppName}";  Filename: "{app}\{#AppExe}"; Tasks: startupicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Run {#AppName} now"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Nothing to stop: the driver is a plain console program with no service or driver of its own.

[Code]
function InitializeSetup(): Boolean;
var
  Msg: string;
begin
  Result := True;
  if not FileExists(ExpandConstant('{sys}\teVirtualMIDI64.dll')) then
  begin
    Msg :=
      'No virtual MIDI driver was found on this PC.' + #13#10#13#10 +
      'Maschine Jam MIDI needs a MIDI port that your other apps can open, and Windows cannot' + #13#10 +
      'make one by itself. The usual answer is loopMIDI (free, from tobias-erichsen.de).' + #13#10#13#10 +
      'You can install now and set that up afterwards: the driver waits for the port and' + #13#10 +
      'picks it up as soon as it appears. The Jam and its LEDs work meanwhile.' + #13#10#13#10 +
      'Carry on with the installation?';
    if MsgBox(Msg, mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;
end;
