; MixCast installer (Inno Setup 6, https://jrsoftware.org/isinfo.php)
;
; Normally built by package.ps1, which passes:
;   /DAppVersion=1.0.0  /DSourceDir=<app folder>  /DOutputDir=<dist>  /DRootDir=<MixCast>
;
; Installs per user (no admin prompt) into %LOCALAPPDATA%\Programs\MixCast.
; Users can pick "all users" on the first page if they prefer Program Files.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "dist\MixCast-" + AppVersion
#endif
#ifndef OutputDir
  #define OutputDir "dist"
#endif
#ifndef RootDir
  #define RootDir ".."
#endif
#define Publisher "Iven"

[Setup]
; NEVER change AppId: it's how Windows knows a new version is an update.
AppId={{BA587764-3809-4B0E-A186-2401EC07E3D1}
AppName=MixCast
AppVersion={#AppVersion}
AppVerName=MixCast {#AppVersion}
AppPublisher={#Publisher}
AppCopyright=Copyright (c) 2026 {#Publisher}
VersionInfoVersion={#AppVersion}
VersionInfoProductName=MixCast

DefaultDirName={autopf}\MixCast
DefaultGroupName=MixCast
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; Windows 10 2004 or newer, 64-bit (per-app audio capture needs 2004+).
MinVersion=10.0.19041
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Close a running MixCast before installing/uninstalling (it holds this mutex).
AppMutex=MixCastRunningMutex
CloseApplications=yes

SetupIconFile={#RootDir}\engine\gui\mixcast.ico
UninstallDisplayIcon={app}\mixcast.exe
UninstallDisplayName=MixCast
WizardStyle=modern
OutputDir={#OutputDir}
OutputBaseFilename=MixCast-{#AppVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: "startup";     Description: "&Start MixCast when Windows starts (it waits in the tray)"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\MixCast"; Filename: "{app}\mixcast.exe"
Name: "{autodesktop}\MixCast";  Filename: "{app}\mixcast.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "MixCast"; \
    ValueData: """{app}\mixcast.exe"" --minimized"; Tasks: startup; Flags: uninsdeletevalue

[Run]
Filename: "https://vb-audio.com/Cable/"; Description: "Open the VB-CABLE download page (needed once; skip if you have it)"; \
    Flags: shellexec postinstall skipifsilent unchecked
Filename: "{app}\mixcast.exe"; Description: "Start MixCast now"; Flags: nowait postinstall skipifsilent

[Messages]
FinishedLabel=MixCast is installed.%n%nMixCast sends your mix to Discord through VB-CABLE, a free virtual cable from vb-audio.com. If you don't have it yet, tick the box below, install it, restart your PC, then in Discord choose "CABLE Output" as your input device.

[Code]
// On uninstall, offer to remove settings and soundboard copies as well.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{localappdata}\MixCast');
    if DirExists(DataDir) and not UninstallSilent then
      if MsgBox('Also delete your MixCast settings and soundboard sounds?' + #13#10#13#10 +
                DataDir + #13#10#13#10 +
                'Choose No to keep them for a future reinstall.',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(DataDir, True, True, True);
  end;
end;
