; Inno Setup script for the Avalonia application's packaging prototype (plan step 3.3).
;
; Built by deploy\avalonia\build_avalonia_package.ps1, which stages the files, passes the version read from
; AppVersion (include\constants.h) as /DMyAppVersion, and /DSIGN with /Ssigntool=... to sign the setup and the
; uninstaller. Requires Inno Setup 6.
;
; A PREVIEW, installed BESIDE the shipped Qt application, never over it: its own AppId, its own folder and Start menu
; name, and no .ch10 file association (that belongs to the shipped app until the switch-over, plan step 6.1).

#define MyAppName "tmDataQualityAnalyzer Preview"
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0-dev"
#endif
#define MyAppPublisher "tmDataQualityAnalyzer"
#define MyAppExeName "tmDataQualityAnalyzer.exe"
#define StagingDir "staging\app"

[Setup]
; Not the Qt application's AppId: the two install, upgrade and uninstall independently.
AppId={{29FB1877-4F5C-4699-A9B6-CC652BD8D93A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=.
OutputBaseFilename=tmDataQualityAnalyzer-preview-v{#MyAppVersion}_setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\..\LICENSE
SetupIconFile=..\..\resources\icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
MinVersion=10.0.17763

#ifdef SIGN
SignTool=signtool
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; The single-file application, the three native libraries beside it (each signed by its own publisher), the native
; core, the licence and the third-party notices.
Source: "{#StagingDir}\*"; DestDir: "{app}"; Excludes: "settings"; Flags: ignoreversion recursesubdirs createallsubdirs

; The settings files, as the Qt installer ships them (US8.0): a file the user already has is never overwritten, on an
; upgrade or a reinstall, and uninstalling leaves them.
Source: "{#StagingDir}\settings\*"; DestDir: "{app}\settings"; Flags: recursesubdirs createallsubdirs onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Third-party notices"; Filename: "{app}\THIRD-PARTY-NOTICES.txt"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
