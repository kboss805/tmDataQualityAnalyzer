; Inno Setup script for the application from v3.0.0 on: the Avalonia application (plan step 5.3).
;
; Built by deploy\build_package.ps1, which stages the files, passes the version read from
; AppVersion (src\core\include\constants.h) as /DMyAppVersion, and /DSIGN with /Ssigntool=... to sign the setup and the
; uninstaller. Requires Inno Setup 6.
;
; It REPLACES the v2.x application rather than installing beside it: the v2.x installer's AppId, its folder and
; Start menu name, and the .ch10 association. Installing it over v2.x upgrades that install in place: the v2.x
; application's bin\ folder and its text guide go, and the user's settings files stay (US8.0). The window
; preferences (theme, recent files and the like) start from the defaults: v3.0.x imported v2.x's INI file, 3.1.0 does not.

#define MyAppName "tmDataQualityAnalyzer"
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0-dev"
#endif
#define MyAppPublisher "tmDataQualityAnalyzer"
#define MyAppExeName "tmDataQualityAnalyzer.exe"
#define StagingDir "staging\app"
#define ManualFile "staging\manual\UserManual.html"

[Setup]
; v2.x's AppId (its deploy\tmDataQualityAnalyzer.iss, since deleted): v3.0.0 is its upgrade, not a second application.
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=.
OutputBaseFilename=tmDataQualityAnalyzer-v{#MyAppVersion}_setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
SetupIconFile=..\assets\icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
MinVersion=10.0.17763
InfoBeforeFile=RELEASENOTES.txt
ChangesAssociations=yes

#ifdef SIGN
SignTool=signtool
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked
Name: "fileassoc"; Description: "Associate .ch10 files with {#MyAppName}"; GroupDescription: "File associations:"
; The BASE manual is compiled into the executable and is always reachable from Help > User Manual, so declining this
; still leaves the user with a manual (plan step 4.9).
Name: "fullmanual"; Description: "Install the full illustrated user manual"; GroupDescription: "Documentation:"

[InstallDelete]
; An upgrade from v2.x: v2.x lived in bin\ (the exe, its runtime and its plugins), and Inno leaves
; files behind when they stop being in the package.
Type: filesandordirs; Name: "{app}\bin"
; v2.x's text guide, which describes it and not this application.
Type: files; Name: "{app}\UserGuide.txt"
; The full manual: Help opens a UserManual.html beside the exe when there is one, and v2.x's sat exactly there. Taken
; away first, so that declining the task leaves the compiled-in manual rather than v2.x's.
Type: files; Name: "{app}\UserManual.html"

[Files]
; The single-file application, the three native libraries beside it (each signed by its own publisher), the native
; core, the licence and the third-party notices.
Source: "{#StagingDir}\*"; DestDir: "{app}"; Excludes: "settings"; Flags: ignoreversion recursesubdirs createallsubdirs

; The settings files (US8.0): a file the user already has is never overwritten, on an upgrade or a reinstall, and
; uninstalling leaves them. v2.x kept them in the same settings\ folder, so an upgrade keeps the user's.
Source: "{#StagingDir}\settings\*"; DestDir: "{app}\settings"; Flags: recursesubdirs createallsubdirs onlyifdoesntexist uninsneveruninstall

; The full user manual, optional. Its absence is a supported state: Help opens the manual compiled into the executable.
Source: "{#ManualFile}"; DestDir: "{app}"; Flags: ignoreversion; Tasks: fullmanual

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Third-party notices"; Filename: "{app}\THIRD-PARTY-NOTICES.txt"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; The .ch10 association, as v2.x wrote it, now pointing at the exe in the install folder rather than in bin\.
Root: HKA; Subkey: "Software\Classes\.ch10"; ValueType: string; ValueName: ""; ValueData: "tmDataQualityAnalyzer.ch10"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\tmDataQualityAnalyzer.ch10"; ValueType: string; ValueName: ""; ValueData: "Chapter 10 Telemetry File"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\tmDataQualityAnalyzer.ch10\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\tmDataQualityAnalyzer.ch10\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc
; Declined on an upgrade: v2.x's association would still open bin\'s exe, which the upgrade has just deleted.
Root: HKA; Subkey: "Software\Classes\tmDataQualityAnalyzer.ch10"; Flags: deletekey dontcreatekey; Tasks: not fileassoc
; And .ch10 itself, which would otherwise still name the ProgID just deleted, so Explorer offers a dead entry rather than
; asking. Only when it names OUR ProgID (Ch10NamesOurProgId): another application's association is left alone.
Root: HKA; Subkey: "Software\Classes\.ch10"; ValueType: none; ValueName: ""; Flags: deletevalue dontcreatekey; Tasks: not fileassoc; Check: Ch10NamesOurProgId

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
// Whether .ch10's default value names this application's ProgID (in the same root the [Registry] entries write, HKA:
// HKLM for an administrative install, HKCU for a per-user one).
function Ch10NamesOurProgId(): Boolean;
var
  ProgId: String;
begin
  Result := RegQueryStringValue(HKA, 'Software\Classes\.ch10', '', ProgId) and (CompareText(ProgId, 'tmDataQualityAnalyzer.ch10') = 0);
end;
