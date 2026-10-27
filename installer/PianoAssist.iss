; Inno Setup script for the PianoAssist Windows installer.
;
; Built by CI from the Release exe:
;   ISCC /DAppVersion=0.1.0 /DSourceExe=path\to\PianoAssist.exe installer\PianoAssist.iss
;
; Per-user install: no administrator rights, nothing outside the user's
; profile. Settings and logs in %APPDATA%\PianoAssist are kept on uninstall.
;
; Code signing: credentials never live in this repository. To sign, define
; a sign tool named "pianoassist" in the Inno Setup IDE or on the command line
; (ISCC "/Spianoassist=signtool sign /fd sha256 /tr <timestamp-url> /f <cert> /p <password> $f")
; and pass /DSign. Without /DSign the installer is built unsigned.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceExe
  #define SourceExe "..\build\windows-x64\src\app\PianoAssistApp_artefacts\Release\PianoAssist.exe"
#endif

[Setup]
AppId={{6F0B7E1C-5C2B-4C8E-9D4B-3B1E2A9C7F10}
AppName=PianoAssist
AppVersion={#AppVersion}
AppPublisher=PianoAssist
DefaultDirName={localappdata}\Programs\PianoAssist
DefaultGroupName=PianoAssist
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename=PianoAssist-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\PianoAssist.exe
#ifdef Sign
SignTool=pianoassist
SignedUninstaller=yes
#endif

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "openwith"; Description: "Offer PianoAssist in ""Open with"" for MIDI files (.mid, .midi)"

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; Flags: ignoreversion

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
Filename: "{app}\PianoAssist.exe"; Description: "Start PianoAssist"; Flags: nowait postinstall skipifsilent
