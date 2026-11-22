; Copyright (c) 2026 UniVex Studios. All Rights Reserved.
;
; The UniVex Hub installer for Windows: a standard setup wizard (welcome, install folder, Start menu
; folder, shortcuts and file association, progress, finish with "Launch UniVex Hub") and an
; uninstaller in Apps & features. Compiled by Inno Setup's ISCC, run by
; Engine/Tools/make_hub_installer.py, which passes every value below with /D so names, version and
; contents come from one place (the C++ headers the Hub reads, checked by test_shared_names.py).

#ifndef AppId
  #error Build this script through Engine/Tools/make_hub_installer.py
#endif

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
VersionInfoVersion={#NumericVersion}
VersionInfoProductName={#AppName}
VersionInfoCompany={#Publisher}
VersionInfoDescription={#AppName} Setup
; Per-user by default, so no administrator rights are needed; the wizard offers "all users" too.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#InstallFolder}
DefaultGroupName={#AppName}
DisableProgramGroupPage=auto
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\{#HubExe}
UninstallDisplayName={#AppName}
ChangesAssociations=yes
CloseApplications=yes
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "fileassoc"; Description: "Open {#ProjectExtension} project files with {#AppName}"; GroupDescription: "File association:"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#HubExe}"; Comment: "{#Description}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#HubExe}"; Comment: "{#Description}"; Tasks: desktopicon

[Registry]
; HKA is the current user's classes for a per-user install, the machine's for an all-users one.
Root: HKA; Subkey: "Software\Classes\{#ProjectExtension}"; ValueType: string; ValueName: ""; ValueData: "{#ProgramId}"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgramId}"; ValueType: string; ValueName: ""; ValueData: "{#ProjectTypeName}"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgramId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#WindowsIcon}"",0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgramId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#HubExe}"" ""%1"""; Tasks: fileassoc

[Run]
Filename: "{app}\{#HubExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
