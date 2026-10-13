; QIP 2005 / Jimm.online. Packaging only: original payload remains byte-for-byte.
; Inno Setup 6.7.3 or newer. UTF-8 source.
#define PackageVersion "1.1.0"
#ifndef PayloadDir
  #define PayloadDir "..\private\payload"
#endif
#ifndef AgentDir
  #define AgentDir "..\build\agent"
#endif
#ifndef OutputPath
  #define OutputPath "..\dist"
#endif

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
OutputDir={#OutputPath}
OutputBaseFilename=QIP2005-Jimm-Setup-{#PackageVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\private\qip.ico
UninstallDisplayIcon={app}\qip.exe
UninstallDisplayName=QIP 2005 (Jimm.online)
UninstallFilesDir={app}\Uninstall
Uninstallable=yes
UninstallLogMode=append
CloseApplications=no
RestartApplications=no
SetupLogging=yes
VersionInfoVersion={#PackageVersion}.0
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
Source: "{#PayloadDir}\Users\connect.cfg"; DestDir: "{app}\Users"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#PayloadDir}\LI\current.cfg"; DestDir: "{app}\LI"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#PayloadDir}\LI\langs.cfg"; DestDir: "{app}\LI"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#PayloadDir}\Skins\current.cfg"; DestDir: "{app}\Skins"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#PayloadDir}\Skins\skins.cfg"; DestDir: "{app}\Skins"; Flags: onlyifdoesntexist uninsneveruninstall
; No wildcard User data entry. All remaining archive files are program resources.
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Excludes: "\Users\*,\LI\current.cfg,\LI\langs.cfg,\Skins\current.cfg,\Skins\skins.cfg"; Flags: ignoreversion recursesubdirs createallsubdirs

Source: "{#AgentDir}\QipUpdateAgent.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AgentDir}\QipLauncher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AgentDir}\installed-version.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AgentDir}\Licenses\*"; DestDir: "{app}\Licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#AgentDir}\QipUpdateAgent.exe"; DestName: "QipInstallerBackupHelper.exe"; Flags: dontcopy

[InstallDelete]
; Switching off a task during reinstall removes only our previously installed shortcut.
Type: files; Name: "{userstartup}\QIP 2005 (Jimm.online).lnk"; Tasks: not autostart; Check: OwnStartupShortcut
Type: files; Name: "{userdesktop}\QIP 2005 (Jimm.online).lnk"; Tasks: not desktopicon; Check: OwnDesktopShortcut
[Icons]
Name: "{group}\QIP 2005 (Jimm.online)"; Filename: "{app}\QipLauncher.exe"; WorkingDir: "{app}"; IconFilename: "{app}\qip.exe"
Name: "{group}\Удалить QIP 2005"; Filename: "{uninstallexe}"; WorkingDir: "{app}"
Name: "{userdesktop}\QIP 2005 (Jimm.online)"; Filename: "{app}\QipLauncher.exe"; WorkingDir: "{app}"; IconFilename: "{app}\qip.exe"; Tasks: desktopicon
Name: "{userstartup}\QIP 2005 (Jimm.online)"; Filename: "{app}\QipLauncher.exe"; WorkingDir: "{app}"; IconFilename: "{app}\qip.exe"; Tasks: autostart

; No services, scheduler tasks, registry Run values or client autorun at setup completion.
; No blanket [UninstallDelete] of {app}.

Name: "{group}\Настройки обновлений"; Filename: "{app}\QipUpdateAgent.exe"; Parameters: "--settings"; WorkingDir: "{app}"

[Code]
const
  ReparsePointAttribute = $400;
var
  RemoveUserData: Boolean;
  UpdateOptions: TInputOptionWizardPage;
  BackupMade: Boolean;

function OwnShortcut(const Name: String): Boolean;
var
  Shell, Link: Variant;
  Target: String;
begin
  Result := False;
  if not FileExists(Name) then Exit;
  try
    Shell := CreateOleObject('WScript.Shell');
    Link := Shell.CreateShortcut(Name);
    Target := Link.TargetPath;
    Result := (CompareText(Target, ExpandConstant('{app}\qip.exe')) = 0) or
      (CompareText(Target, ExpandConstant('{app}\QipLauncher.exe')) = 0);
  except
    Result := False;
  end;
end;

function OwnStartupShortcut: Boolean;
begin
  Result := OwnShortcut(ExpandConstant('{userstartup}\QIP 2005 (Jimm.online).lnk'));
end;

function OwnDesktopShortcut: Boolean;
begin
  Result := OwnShortcut(ExpandConstant('{userdesktop}\QIP 2005 (Jimm.online).lnk'));
end;


function ClientIsRunning: Boolean;
var
  Locator, Services, Processes, Item: Variant;
  I: Integer;
  Path: String;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Services := Locator.ConnectServer('', 'root\CIMV2');
    Processes := Services.ExecQuery('SELECT ExecutablePath FROM Win32_Process WHERE Name="qip.exe" OR Name="QipUpdateAgent.exe" OR Name="QipLauncher.exe"');
    for I := 0 to Processes.Count - 1 do
    begin
      Item := Processes.ItemIndex(I);
      if VarIsNull(Item.ExecutablePath) then
      begin
        Result := True;
        Exit;
      end;
      Path := Item.ExecutablePath;
      if CompareText(RemoveBackslashUnlessRoot(ExtractFileDir(Path)),
        RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) = 0 then
      begin
        Result := True;
        Exit;
      end;
    end;
  except
    // Fail closed if the process list cannot be inspected.
    Result := True;
  end;
end;

procedure InitializeWizard;
var
  Existing: AnsiString;
  Text: String;
  Selected: String;
begin
  if (not WizardSilent) and OwnStartupShortcut then
  begin
    Selected := WizardSelectedTasks(False);
    if Selected <> '' then Selected := Selected + ',';
    WizardSelectTasks(Selected + 'autostart');
  end;
  UpdateOptions := CreateInputOptionPage(wpSelectTasks, 'Обновления QIP',
    'Проверка подписанных обновлений из GitHub Releases',
    'Проверка сети выполняется отдельно от клиента. Установка возможна только после закрытия QIP.', False, False);
  UpdateOptions.Add('Автоматически проверять и скачивать обновления (раз в 24 часа)');
  UpdateOptions.Add('Устанавливать автоматически после закрытия QIP');
  UpdateOptions.Values[0] := True;
  UpdateOptions.Values[1] := False;
  if LoadStringFromFile(ExpandConstant('{localappdata}\QIP2005\UpdateAgent\settings.json'), Existing) then
  begin
    Text := String(Existing);
    StringChangeEx(Text, ' ', '', True);
    StringChangeEx(Text, #13, '', True);
    StringChangeEx(Text, #10, '', True);
    StringChangeEx(Text, #9, '', True);
    UpdateOptions.Values[0] := Pos('"check_automatically":false', Text) = 0;
    UpdateOptions.Values[1] := Pos('"install_after_exit":true', Text) <> 0;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ExitCode: Integer;
  Params: String;
begin
  if CurStep = ssPostInstall then
  begin
    if (not WizardSilent) or
      (not FileExists(ExpandConstant('{localappdata}\QIP2005\UpdateAgent\settings.json'))) then
    begin
      Params := '--configure ';
      if UpdateOptions.Values[0] then Params := Params + '1 ' else Params := Params + '0 ';
      if UpdateOptions.Values[1] then Params := Params + '1' else Params := Params + '0';
      if not Exec(ExpandConstant('{app}\QipUpdateAgent.exe'), Params,
        ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then
        RaiseException('Не удалось сохранить настройки обновления.');
      if ExitCode <> 0 then RaiseException('Обновлятор не сохранил настройки.');
    end;
  end;
end;

function InitializeUninstall: Boolean;
begin
  Result := not ClientIsRunning;
  if not Result then
    MsgBox('Закройте QIP и окна обновлятора. Подождите до 30 секунд и повторите удаление.', mbInformation, MB_OK);
end;

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
  BackupId: String;
  BackupExitCode: Integer;
begin
  Result := '';
  if ClientIsRunning then
  begin
    Result := 'Закройте QIP и окна обновлятора. Подождите до 30 секунд и повторите установку.';
    Exit;
  end;
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
  if (Result = '') and (not BackupMade) and FileExists(ExpandConstant('{app}\qip.exe')) then
  begin
    ExtractTemporaryFile('QipInstallerBackupHelper.exe');
    BackupId := 'manual-' + GetDateTimeString('yyyymmddhhnnss', '-', '-') + '-' +
      ExtractFileName(ExpandConstant('{tmp}'));
    StringChangeEx(BackupId, '.', '_', True);
    if not Exec(ExpandConstant('{tmp}\QipInstallerBackupHelper.exe'),
      '--backup-for-install "' + ExpandConstant('{app}') + '" "' + BackupId + '"',
      ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, BackupExitCode) then
    begin
      Result := 'Не удалось создать резервную копию. Установка остановлена.';
      Exit;
    end;
    if BackupExitCode <> 0 then
    begin
      Result := 'Резервная копия не создана. Файлы QIP не изменялись.';
      Exit;
    end;
    BackupMade := True;
  end;
end;

procedure RemoveMutableData;
var
  DataDir, LIPath, SkinsPath, AgentData: String;
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
  AgentData := ExpandConstant('{localappdata}\QIP2005\UpdateAgent');
  if not IsReparsePoint(AgentData) then
  begin
    if DirExists(AgentData) then OK := DelTree(AgentData, True, True, True) and OK;
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
        'Профили: ' + ExpandConstant('{app}\Users') + #13#10 +
        'Также будут удалены настройки языка/скина, настройки обновлятора и его резервные копии.' + #13#10 +
        'Нажмите «Нет», чтобы сохранить данные для повторной установки.',
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        RemoveUserData := MsgBox('Подтвердите безвозвратное удаление профилей и истории.' + #13#10 +
          'Резервная копия не создаётся.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
    end;
  end;
  if (CurUninstallStep = usPostUninstall) and RemoveUserData then
    RemoveMutableData;
end;
