; CloseCrab-Unified Inno Setup Script
; Requires Inno Setup 6.x: https://jrsoftware.org/isinfo.php

#define MyAppName "CloseCrab"
#define MyAppVersion "0.3.1"
#define MyAppPublisher "Blitzball Labs"
#define MyAppURL "https://github.com/Blitzball996/CloseCrab-Unified"
#define MyAppExeName "closecrab.exe"

[Setup]
AppId={{B7A3F2E1-4C5D-6E7F-8A9B-0C1D2E3F4A5B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=..\dist
OutputBaseFilename=CloseCrab-Setup-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\icons\closecrab.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
PrivilegesRequired=lowest

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "addtopath"; Description: "Add to PATH"; GroupDescription: "System integration:"
Name: "registercontext"; Description: "Add 'Open CloseCrab here' to Explorer context menu"; GroupDescription: "System integration:"

[Files]
; Main executable
Source: "..\build\Release\closecrab.exe"; DestDir: "{app}"; Flags: ignoreversion
; DLLs
Source: "..\build\Release\*.dll"; DestDir: "{app}"; Flags: ignoreversion
; Config template
Source: "..\config\config.yaml.example"; DestDir: "{app}\config"; Flags: ignoreversion; DestName: "config.yaml"
; Documentation
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent shellexec

[Registry]
; Add to PATH
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Tasks: addtopath; Check: NeedsAddPath(ExpandConstant('{app}'))
; Context menu
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\CloseCrab"; ValueType: string; ValueData: "Open CloseCrab here"; Tasks: registercontext
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\CloseCrab"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#MyAppExeName}"""; Tasks: registercontext
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\CloseCrab\command"; ValueType: string; ValueData: """{app}\{#MyAppExeName}"" --cwd ""%V"""; Tasks: registercontext

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\closecrab.log"
Type: files; Name: "{app}\trace.log"
Type: files; Name: "{app}\crash.log"
Type: dirifempty; Name: "{app}\config"

[Code]
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Param + ';', ';' + OrigPath + ';') = 0;
end;
