// Used by the real installer and the signed E2E fixture.
// GetCustomSetupExitCode makes a post-install preparation error visible to old workers too.
var
  ProfileFinalizationFailed: Boolean;
function GetCustomSetupExitCode: Integer;
begin
  if ProfileFinalizationFailed then Result := 23 else Result := 0;
end;

procedure FinalizeInstalledProfile;
var
  ProfileExitCode: Integer;
begin
  if not Exec(ExpandConstant('{app}\QipUpdateAgent.exe'), '--prepare-profile-from-installer',
    ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ProfileExitCode) then
  begin
    ProfileFinalizationFailed := True;
    RaiseException('Не удалось подготовить профиль QIP.');
  end;
  if ProfileExitCode <> 0 then
  begin
    ProfileFinalizationFailed := True;
    RaiseException('Профиль QIP не подготовлен. Резервная копия сохранена.');
  end;
end;
