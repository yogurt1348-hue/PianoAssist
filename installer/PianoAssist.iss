; Inno Setup script for the PianoAssist Windows installer.
;
; Built by CI (installer/package.ps1) from the Release exe:
;   ISCC /DAppVersion=0.2.0 /DSourceExe=path\to\PianoAssist.exe /DLicenses=path\to\licenses installer\PianoAssist.iss
;
; Per-user install: no administrator rights, nothing outside the user's
; profile. The program goes to %LOCALAPPDATA%\Programs\PianoAssist; settings,
; logs and the song library live in %APPDATA%\PianoAssist and are never
; written to the program folder, so an update or an uninstall keeps them.
;
; Updating: running a newer setup over an older one replaces the program in
; place (same AppId). If PianoAssist is running, setup asks to close it
; first; when the user declines and a file stays locked, setup offers
; Retry / Abort and leaves the old version working. An interrupted setup
; can simply be run again.
;
; Code signing: credentials never live in this repository. CI signs only when
; the signing secrets are configured (see docs/windows-warnings.md). To sign
; by hand, define a sign tool named "pianoassist"
; (ISCC "/Spianoassist=signtool sign /fd sha256 /tr <timestamp-url> /td sha256 /f <cert> /p <password> $f")
; and pass /DSign. Without /DSign the installer is built unsigned.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceExe
  #define SourceExe "..\build\windows-x64\src\app\PianoAssistApp_artefacts\Release\PianoAssist.exe"
#endif
#ifndef Licenses
  #define Licenses "..\licenses"
#endif

[Setup]
; Never change the AppId: it is how Windows and setup recognise an update.
AppId={{6F0B7E1C-5C2B-4C8E-9D4B-3B1E2A9C7F10}
AppName=PianoAssist
AppVersion={#AppVersion}
AppVerName=PianoAssist {#AppVersion}
AppPublisher=PianoAssist
AppPublisherURL=https://github.com/yogurt1348-hue/PianoAssist
AppSupportURL=https://github.com/yogurt1348-hue/PianoAssist/issues
AppUpdatesURL=https://github.com/yogurt1348-hue/PianoAssist/releases
VersionInfoVersion={#AppVersion}
VersionInfoProductName=PianoAssist
VersionInfoDescription=PianoAssist setup
DefaultDirName={localappdata}\Programs\PianoAssist
DefaultGroupName=PianoAssist
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename=PianoAssist-{#AppVersion}-windows-x64-setup
SetupIconFile=..\assets\pianoassist.ico
UninstallDisplayIcon={app}\PianoAssist.exe
UninstallDisplayName=PianoAssist
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Ask to close a running PianoAssist before files are replaced; never restart it by itself.
CloseApplications=yes
CloseApplicationsFilter=PianoAssist.exe
RestartApplications=no
#ifdef Sign
SignTool=pianoassist
SignedUninstaller=yes
#endif

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
ru.DesktopIcon=Создать ярлык на рабочем столе
en.DesktopIcon=Create a desktop shortcut
ru.OpenWith=Добавить PianoAssist в «Открыть с помощью» для MIDI-файлов (.mid, .midi)
en.OpenWith=Offer PianoAssist in "Open with" for MIDI files (.mid, .midi)
ru.StartApp=Запустить PianoAssist
en.StartApp=Start PianoAssist

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; Flags: unchecked
Name: "openwith"; Description: "{cm:OpenWith}"

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Licenses}\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs

[InstallDelete]
; A portable marker must never make the installed copy portable.
Type: files; Name: "{app}\PianoAssist.portable"

[Icons]
Name: "{autoprograms}\PianoAssist"; Filename: "{app}\PianoAssist.exe"
Name: "{autodesktop}\PianoAssist"; Filename: "{app}\PianoAssist.exe"; Tasks: desktopicon

[Registry]
; "Open with" only: the existing default program for .mid is left alone.
Root: HKCU; Subkey: "Software\Classes\Applications\PianoAssist.exe\shell\open\command"; ValueType: string; ValueData: """{app}\PianoAssist.exe"" ""%1"""; Flags: uninsdeletekey; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\Applications\PianoAssist.exe\SupportedTypes"; ValueType: string; ValueName: ".mid"; ValueData: ""; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\Applications\PianoAssist.exe\SupportedTypes"; ValueType: string; ValueName: ".midi"; ValueData: ""; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.mid\OpenWithList\PianoAssist.exe"; Flags: uninsdeletekey; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.midi\OpenWithList\PianoAssist.exe"; Flags: uninsdeletekey; Tasks: openwith

[Run]
Filename: "{app}\PianoAssist.exe"; Description: "{cm:StartApp}"; Flags: nowait postinstall skipifsilent
