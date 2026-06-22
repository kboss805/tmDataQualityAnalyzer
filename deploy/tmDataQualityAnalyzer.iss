; tmDataQualityAnalyzer Inno Setup Script
; Version is passed in from build_release.cmd via /DMyAppVersion=X.Y.Z
; which reads the authoritative version from include/constants.h.
;
; Requires Inno Setup 6.x (https://jrsoftware.org/isinfo.php)
;
; Build via build script (recommended — picks up version automatically):
;   deploy\build_release.cmd
;
; Build manually (uses fallback version string):
;   iscc /DMyAppVersion=X.Y.Z deploy\tmDataQualityAnalyzer.iss

#define MyAppName "tmDataQualityAnalyzer"
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0-dev"
#endif
#define MyAppPublisher "tmDataQualityAnalyzer"
#define MyAppExeName "tmDataQualityAnalyzer.exe"
#define StagingDir "staging\installer"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=.
OutputBaseFilename=tmDataQualityAnalyzer-v{#MyAppVersion}_setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
SetupIconFile=..\resources\icon.ico
UninstallDisplayIcon={app}\bin\{#MyAppExeName}
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
MinVersion=10.0.17763
InfoBeforeFile=RELEASENOTES.txt
ChangesAssociations=yes

; Code signing — enabled when build_release.cmd passes /DSIGN /Ssigntool=... to iscc.
; Without /DSIGN, the installer compiles unsigned (useful for testing).
#ifdef SIGN
SignTool=signtool
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked
Name: "fileassoc"; Description: "Associate .ch10 files with {#MyAppName}"; GroupDescription: "File associations:"

[Files]
; Application binaries and Qt dependencies (from bin/ staging)
Source: "{#StagingDir}\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs createallsubdirs

; User Guide
Source: "..\UserGuide.txt"; DestDir: "{app}"; Flags: ignoreversion

; Settings files (receiver_params\, rcvr_cals\, framesync_patterns\) — preserve
; existing user TOML on upgrade; only files not already present are copied.
Source: "{#StagingDir}\settings\*"; DestDir: "{app}\settings"; Flags: recursesubdirs createallsubdirs onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.ch10"; ValueType: string; ValueName: ""; ValueData: "tmDataQualityAnalyzer.ch10"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\tmDataQualityAnalyzer.ch10"; ValueType: string; ValueName: ""; ValueData: "Chapter 10 Telemetry File"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\tmDataQualityAnalyzer.ch10\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\{#MyAppExeName},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\tmDataQualityAnalyzer.ch10\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc

[Run]
Filename: "{app}\bin\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
