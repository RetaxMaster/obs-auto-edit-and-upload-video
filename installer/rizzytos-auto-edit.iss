#define MyAppName "Rizzytos Auto Edit"
#define MyAppPublisher "RetaxMaster"
#ifndef MyAppVersion
#define MyAppVersion "1.0.0"
#endif

[Setup]
AppId={{9D1A4BC2-54F1-4D7E-9D9A-9F6F441AF7D1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
UninstallDisplayName={#MyAppName}
DefaultDirName={pf}\obs-studio
DirExistsWarning=no
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=RizzytosAutoEdit-Setup-x64
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern

[Files]
Source: "..\release\obs-plugins\64bit\rizzytos-auto-edit.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "..\release\data\obs-plugins\rizzytos-auto-edit\*"; DestDir: "{app}\data\obs-plugins\rizzytos-auto-edit"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: files; Name: "{app}\obs-plugins\64bit\rizzytos-auto-edit.dll"
Type: filesandordirs; Name: "{app}\data\obs-plugins\rizzytos-auto-edit"

[Code]
function IsObsRunning(): Boolean;
var
  ResultCode: Integer;
begin
  Result :=
    Exec(
      ExpandConstant('{cmd}'),
      '/C tasklist /FI "IMAGENAME eq obs64.exe" | find /I "obs64.exe" >NUL',
      '',
      SW_HIDE,
      ewWaitUntilTerminated,
      ResultCode
    ) and (ResultCode = 0);
end;

function InitializeSetup(): Boolean;
begin
  if IsObsRunning() then begin
    MsgBox('OBS Studio is currently running. Close OBS before installing or updating Rizzytos Auto Edit.', mbError, MB_OK);
    Result := False;
  end else begin
    Result := True;
  end;
end;
