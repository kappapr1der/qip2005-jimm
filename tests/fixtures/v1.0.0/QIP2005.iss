; QIP 2005 / Jimm.online. Packaging only: original payload remains byte-for-byte.
; Inno Setup 6.7.3 or newer. UTF-8 source.
#define PackageVersion "1.0.0"

[Setup]
AppId={{A94D4B7F-5D7B-4E78-BF72-C44A3AC98849}
AppName=QIP 2005 (Jimm.online)
AppVersion={#PackageVersion}
AppVerName=QIP 2005 (Jimm.online) — установщик {#PackageVersion}
AppPublisher=Локальная упаковка QIP 2005
DefaultDirName={localappdata}\Programs\QIP2005
DisableDirPage=yes
UsePreviousAppDir=no
DefaultGroupName=QIP 2005 (Jimm.online)
DisableProgramGroupPage=yes
AllowNoIcons=no
PrivilegesRequired=lowest
MinVersion=10.0
OutputDir=..
OutputBaseFilename=QIP2005-Jimm-Setup-{#PackageVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=qip.ico
UninstallDisplayIcon={app}\qip.exe
UninstallDisplayName=QIP 2005 (Jimm.online)
UninstallFilesDir={app}\Uninstall
Uninstallable=yes
UninstallLogMode=append
CloseApplications=yes
CloseApplicationsFilter=qip.exe,docking.dll
RestartApplications=no
SetupLogging=yes
VersionInfoVersion=1.0.0.0
VersionInfoDescription=Установщик QIP 2005 для Jimm.online
VersionInfoProductName=QIP 2005 (Jimm.online)
InfoBeforeFile=install-info.txt

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на рабочем столе"; GroupDescription: "Ярлыки:"; Flags: checkedonce
Name: "autostart"; Description: "Запускать QIP при входе текущего пользователя в Windows"; GroupDescription: "Автозапуск:"; Flags: unchecked

[Dirs]
Name: "{app}\Users"; Flags: uninsneveruninstall

[Files]
; Mutable defaults: seed only; never overwrite on repair/upgrade or remove implicitly.
Source: "payload\Users\connect.cfg"; DestDir: "{app}\Users"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "payload\LI\current.cfg"; DestDir: "{app}\LI"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "payload\LI\langs.cfg"; DestDir: "{app}\LI"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "payload\Skins\current.cfg"; DestDir: "{app}\Skins"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "payload\Skins\skins.cfg"; DestDir: "{app}\Skins"; Flags: onlyifdoesntexist uninsneveruninstall
; No wildcard User data entry. All remaining archive files are program resources.
Source: "payload\*"; DestDir: "{app}"; Excludes: "\Users\*,\LI\current.cfg,\LI\langs.cfg,\Skins\current.cfg,\Skins\skins.cfg"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Switching off a task during reinstall removes only our previously installed shortcut.
Type: files; Name: "{userstartup}\QIP 2005 (Jimm.online).lnk"; Tasks: not autostart; Check: WasInstalledHere
Type: files; Name: "{userdesktop}\QIP 2005 (Jimm.online).lnk"; Tasks: not desktopicon; Check: WasInstalledHere
[Icons]
Name: "{group}\QIP 2005 (Jimm.online)"; Filename: "{app}\qip.exe"; WorkingDir: "{app}"; IconFilename: "{app}\qip.exe"
Name: "{group}\Удалить QIP 2005"; Filename: "{uninstallexe}"; WorkingDir: "{app}"
Name: "{userdesktop}\QIP 2005 (Jimm.online)"; Filename: "{app}\qip.exe"; WorkingDir: "{app}"; IconFilename: "{app}\qip.exe"; Tasks: desktopicon
Name: "{userstartup}\QIP 2005 (Jimm.online)"; Filename: "{app}\qip.exe"; WorkingDir: "{app}"; IconFilename: "{app}\qip.exe"; Tasks: autostart

; No [Run], [UninstallRun], downloader, registry Run value, scheduled task or service.
; No blanket [UninstallDelete] of {app}.

[Code]
const
  ReparsePointAttribute = $400;
var
  RemoveUserData: Boolean;

function WasInstalledHere: Boolean;
var
  PreviousDir: String;
begin
  Result := False;
  if RegQueryStringValue(HKCU,
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A94D4B7F-5D7B-4E78-BF72-C44A3AC98849}_is1',
    'InstallLocation', PreviousDir) then
    Result := CompareText(RemoveBackslashUnlessRoot(PreviousDir),
      RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) = 0;
end;
function IsReparsePoint(const Path: String): Boolean;
var
  FindRec: TFindRec;
begin
  Result := False;
  if FindFirst(Path, FindRec) then
  begin
    Result := (FindRec.Attributes and ReparsePointAttribute) <> 0;
    FindClose(FindRec);
  end;
end;

function SafeAppDirectory: Boolean;
var
  AppDir: String;
begin
  AppDir := RemoveBackslashUnlessRoot(ExpandFileName(ExpandConstant('{app}')));
  Result := (CompareText(ExtractFileName(AppDir), 'QIP2005') = 0) and
    (not IsReparsePoint(AppDir));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  DataDir, Probe: String;
begin
  Result := '';
  if not SafeAppDirectory then
  begin
    Result := 'Каталог установки должен называться QIP2005 и быть обычной папкой.';
    Exit;
  end;
  DataDir := ExpandConstant('{app}\Users');
  if IsReparsePoint(DataDir) then
  begin
    Result := 'Папка Users является ссылкой. Установка остановлена, чтобы сохранить данные.';
    Exit;
  end;
  if not ForceDirectories(DataDir) then
  begin
    Result := 'Не удалось создать папку профилей: ' + DataDir;
    Exit;
  end;
  try
    Probe := GenerateUniqueName(DataDir, '.tmp');
    if not SaveStringToFile(Probe, 'QIP installer write check', False) then
      Result := 'Нет доступа для записи профилей: ' + DataDir;
    if not DeleteFile(Probe) then
      Result := 'Не удалось удалить проверочный файл: ' + Probe;
  except
    Result := 'Нет доступа для записи профилей: ' + DataDir;
  end;
end;

procedure RemoveMutableData;
var
  DataDir, LIPath, SkinsPath: String;
  OK: Boolean;
begin
  if not SafeAppDirectory then
  begin
    Log('User data cleanup skipped: unexpected install directory or reparse point.');
    Exit;
  end;
  DataDir := ExpandConstant('{app}\Users');
  LIPath := ExpandConstant('{app}\LI');
  SkinsPath := ExpandConstant('{app}\Skins');
  OK := True;
  // Only this installation's Users subtree and four exact config paths.
  if not IsReparsePoint(DataDir) then
  begin
    if DirExists(DataDir) then
      OK := DelTree(DataDir, True, True, True);
  end
  else OK := False;
  if not IsReparsePoint(LIPath) then
  begin
    if FileExists(LIPath + '\current.cfg') then
      OK := DeleteFile(LIPath + '\current.cfg') and OK;
    if FileExists(LIPath + '\langs.cfg') then
      OK := DeleteFile(LIPath + '\langs.cfg') and OK;
    RemoveDir(LIPath);
  end
  else OK := False;
  if not IsReparsePoint(SkinsPath) then
  begin
    if FileExists(SkinsPath + '\current.cfg') then
      OK := DeleteFile(SkinsPath + '\current.cfg') and OK;
    if FileExists(SkinsPath + '\skins.cfg') then
      OK := DeleteFile(SkinsPath + '\skins.cfg') and OK;
    RemoveDir(SkinsPath);
  end
  else OK := False;
  RemoveDir(ExpandConstant('{app}'));
  if not OK then
  begin
    Log('Some user data was retained: inaccessible files or reparse points.');
    if not UninstallSilent then
      MsgBox('Часть данных осталась в папке установки: ' + ExpandConstant('{app}') + #13#10 +
        'Проверьте её вручную. Ссылки на другие папки не очищались.', mbInformation, MB_OK);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RemoveUserData := False;
    if UninstallSilent then
    begin
      // Explicit documented command-line consent; absent parameter always preserves data.
      RemoveUserData := ExpandConstant('{param:REMOVEUSERDATA|0}') = '1';
      Log('Explicit user data removal requested: ' + IntToStr(Ord(RemoveUserData)));
    end
    else
    begin
      if MsgBox('Удалить также профили, настройки и историю переписки?' + #13#10 + #13#10 +
        'Папка: ' + ExpandConstant('{app}\Users') + #13#10 +
        'Также будут удалены настройки языка и скина этой установки.' + #13#10 +
        'Нажмите «Нет», чтобы сохранить данные для повторной установки.',
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        RemoveUserData := MsgBox('Подтвердите безвозвратное удаление профилей и истории.' + #13#10 +
          'Резервная копия не создаётся.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
    end;
  end;
  if (CurUninstallStep = usPostUninstall) and RemoveUserData then
    RemoveMutableData;
end;
