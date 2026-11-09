; Open Office installer script for Inno Setup 6.7.4.
; Build with scripts/build_installer.py, which passes the defines below and records the inputs.
; Packs the verified default-entry runtime unchanged, plus the Visual C++ runtime DLLs from the
; local Visual Studio VC\Redist folder (redistributable per Microsoft's Distributable Code list)
; next to the executables that import them.

#ifndef SourceDir
  #error SourceDir must be defined (the runtime folder to install)
#endif
#ifndef CrtDir
  #error CrtDir must be defined (VC\Redist\MSVC\<version>\x64\Microsoft.VC145.CRT)
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
#ifndef BuildDate
  #error BuildDate must be defined (yyyymmdd)
#endif
#define AppVersion "0.1.0"

[Setup]
AppId={{ABE6780B-54BF-4D7B-B666-31FA29478AAA}
AppName=Open Office
AppVersion={#AppVersion}
AppVerName=Open Office {#AppVersion} 开发版（{#BuildDate}）
AppPublisher=Open Office Project
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName=Open Office
VersionInfoDescription=Open Office 安装程序
VersionInfoCopyright=Open Office Project；ONLYOFFICE 组件版权归 Ascensio System SIA
; Per-user install: the application writes its profile next to its executable, so the
; install folder must stay writable by the user. {autopf} resolves to %LOCALAPPDATA%\Programs.
PrivilegesRequired=lowest
DefaultDirName={autopf}\OpenOffice
DefaultGroupName=Open Office
DisableProgramGroupPage=yes
; Lets /NOICONS (used by the automated install test) skip the Start Menu shortcuts.
AllowNoIcons=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
ShowLanguageDialog=no
InfoBeforeFile=安装前说明.txt
UninstallDisplayName=Open Office（开发版）
UninstallDisplayIcon={app}\OpenOffice.exe
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=4
OutputDir={#OutputDir}
OutputBaseFilename=OpenOffice-Setup-{#AppVersion}-dev-{#BuildDate}-win64

[Languages]
Name: "chs"; MessagesFile: "ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; The runtime exactly as verified, without any profile (user state) it may contain.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "\openoffice-profile"; Flags: ignoreversion recursesubdirs createallsubdirs
; Visual C++ runtime, app-local: OpenOffice.exe/editors_helper.exe load from {app}, the
; converter tools (x2t.exe and others) from {app}\converter.
Source: "{#CrtDir}\msvcp140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#CrtDir}\msvcp140_1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#CrtDir}\msvcp140_2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#CrtDir}\vcruntime140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#CrtDir}\vcruntime140_1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#CrtDir}\msvcp140.dll"; DestDir: "{app}\converter"; Flags: ignoreversion
Source: "{#CrtDir}\msvcp140_1.dll"; DestDir: "{app}\converter"; Flags: ignoreversion
Source: "{#CrtDir}\msvcp140_2.dll"; DestDir: "{app}\converter"; Flags: ignoreversion
Source: "{#CrtDir}\vcruntime140.dll"; DestDir: "{app}\converter"; Flags: ignoreversion
Source: "{#CrtDir}\vcruntime140_1.dll"; DestDir: "{app}\converter"; Flags: ignoreversion

[Icons]
Name: "{group}\Open Office"; Filename: "{app}\OpenOffice.exe"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,Open Office}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Open Office"; Filename: "{app}\OpenOffice.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\OpenOffice.exe"; Description: "{cm:LaunchProgram,Open Office}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; What the program adds next to itself later: the editor profile and the AI models downloaded in 设置.
Type: filesandordirs; Name: "{app}\openoffice-profile"
Type: filesandordirs; Name: "{app}\models"

[Code]
// 设置 → 迁移 moves the whole program folder and points this uninstall entry at the new
// place (Inno Setup: App Path). The uninstall log still names the files at the old place,
// so after a move the program is still there when the logged files are gone: remove it.
var
  MovedApp: String;

function InitializeUninstall(): Boolean;
var
  Key: String;
begin
  Key := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{ABE6780B-54BF-4D7B-B666-31FA29478AAA}_is1';
  if not RegQueryStringValue(HKCU, Key, 'Inno Setup: App Path', MovedApp) then
    if not RegQueryStringValue(HKLM, Key, 'Inno Setup: App Path', MovedApp) then
      MovedApp := '';
  MovedApp := RemoveBackslashUnlessRoot(MovedApp);
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and (Length(MovedApp) > 3)
     and FileExists(AddBackslash(MovedApp) + 'OpenOffice.exe') then
    DelTree(MovedApp, True, True, True);
end;
