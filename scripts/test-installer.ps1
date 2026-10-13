param(
    [Parameter(Mandatory=$true)][string]$ISCC,
    [Parameter(Mandatory=$true)][string]$BaselineSourceDirectory,
    [Parameter(Mandatory=$true)][string]$WorkDirectory,
    [string]$RealBaseline = ''
)
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path -Parent $PSScriptRoot
$taskWork = [IO.Path]::GetFullPath($WorkDirectory)
if (Test-Path -LiteralPath $taskWork) { throw 'Test directory must not exist; previous data will not be overwritten.' }
New-Item -ItemType Directory -Path $taskWork | Out-Null
$taskApp = Join-Path $taskWork 'QIP2005'
$taskTestId = '{0AA78F03-C619-42F9-9A6C-25758C86A1D7}'
$taskProductionId = '{A94D4B7F-5D7B-4E78-BF72-C44A3AC98849}'
$taskKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\'+$taskTestId+'_is1'
$taskGroup = Join-Path ([Environment]::GetFolderPath('Programs')) 'QIP Installer isolated verification'
$taskAgentState = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'QIP2005\UpdateAgent'))
$taskExpectedAgentState = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'QIP2005\UpdateAgent'))
if ($taskAgentState -ne $taskExpectedAgentState) { throw 'Unexpected agent state path.' }
if ((Test-Path -LiteralPath $taskKey) -or (Test-Path -LiteralPath $taskGroup)) { throw 'Test registration or Start-menu group already exists.' }
$taskOriginalAgentState = Join-Path $taskWork 'prior-agent-state'
$taskNewAgentState = Join-Path $taskWork 'test-agent-state'
$taskResults = [Collections.Generic.List[object]]::new()
function Assert-Test([bool]$ok,[string]$name) {
    $taskResults.Add([pscustomobject]@{check=$name;passed=$ok})
    if (-not $ok) { throw "Installer test failed: $name" }
    Write-Output "PASS: $name"
}
function User-Hashes([string]$root) {
    $taskMap = @{}
    foreach ($taskFile in (Get-ChildItem -LiteralPath $root -File -Recurse)) {
        $taskMap[[IO.Path]::GetRelativePath($root,$taskFile.FullName)] = (Get-FileHash -LiteralPath $taskFile.FullName).Hash
    }
    return $taskMap
}
function Same-Hashes($first,$second) {
    if ($first.Count -ne $second.Count) { return $false }
    foreach ($name in $first.Keys) { if ($first[$name] -ne $second[$name]) { return $false } }
    return $true
}
function Build-Test([string]$version,[string]$text) {
    $taskText = $text.Replace($taskProductionId,$taskTestId).Replace('DefaultGroupName=QIP 2005 (Jimm.online)','DefaultGroupName=QIP Installer isolated verification')
    $taskText = $taskText.Replace('QIP 2005 (Jimm.online)', 'QIP Test (isolated)')
    $taskText = $taskText.Replace('OutputDir=..',('OutputDir='+$taskWork)).Replace('OutputDir={#OutputPath}',('OutputDir='+$taskWork))
    $taskText = $taskText.Replace('SetupIconFile=qip.ico',('SetupIconFile='+$taskProject+'\private\qip.ico')).Replace('SetupIconFile=..\private\qip.ico',('SetupIconFile='+$taskProject+'\private\qip.ico'))
    if ($version -eq '1.0.0') {
        $taskText = $taskText.Replace('Source: "payload\',('Source: "'+$taskProject+'\private\payload\')).Replace('InfoBeforeFile=install-info.txt',('InfoBeforeFile='+$BaselineSourceDirectory+'\install-info.txt'))
    } else {
        $taskText = $taskText.Replace('InfoBeforeFile=install-info.txt',('InfoBeforeFile='+$taskProject+'\installer\install-info.txt'))
    }
    $taskScript = Join-Path $taskWork ('test-'+$version+'.iss')
    [IO.File]::WriteAllText($taskScript,$taskText,[Text.UTF8Encoding]::new($true))
    & $ISCC ('/DPayloadDir='+$taskProject+'\private\payload') ('/DAgentDir='+$taskProject+'\build\agent') $taskScript > (Join-Path $taskWork ('compile-'+$version+'.log'))
    if ($LASTEXITCODE -ne 0) { throw 'Test installer compilation failed.' }
}
function Run-TestSetup([string]$version) {
    $taskProcess = Start-Process -FilePath (Join-Path $taskWork ('QIP2005-Jimm-Setup-'+$version+'.exe')) -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/TASKS=""',('/DIR="'+$taskApp+'"'),('/LOG="'+(Join-Path $taskWork ('install-'+$version+'.log'))+'"')) -WindowStyle Hidden -Wait -PassThru
    Assert-Test ($taskProcess.ExitCode -eq 0) "$version installation exit 0"
}
if (Test-Path -LiteralPath $taskAgentState) {
    if ((Get-Item -LiteralPath $taskAgentState).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Agent-state reparse point refused.' }
    # The source is this explicitly named user-state folder; destination is the new test directory.
    Move-Item -LiteralPath $taskAgentState -Destination $taskOriginalAgentState
}
try {
    Build-Test '1.0.0' ([IO.File]::ReadAllText((Join-Path $BaselineSourceDirectory 'QIP2005.iss')))
    Build-Test '1.1.0' ([IO.File]::ReadAllText((Join-Path $taskProject 'installer\QIP2005.iss')))
    Run-TestSetup '1.0.0'
    if ($RealBaseline) {
        Copy-Item -LiteralPath (Join-Path $RealBaseline 'Users') -Destination $taskApp -Recurse -Force
    }
    $taskUsers = Join-Path $taskApp 'Users'
    $taskBefore = User-Hashes $taskUsers
    $taskExeHash = (Get-FileHash -LiteralPath (Join-Path $taskApp 'qip.exe')).Hash
    $taskDllHash = (Get-FileHash -LiteralPath (Join-Path $taskApp 'Plugins\docking.dll')).Hash
    Run-TestSetup '1.1.0'
    Assert-Test (Same-Hashes $taskBefore (User-Hashes $taskUsers)) 'Profiles and history are byte-identical after 1.0.0 to 1.1.0'
    Assert-Test ((Get-FileHash -LiteralPath (Join-Path $taskApp 'qip.exe')).Hash -eq $taskExeHash) 'Original qip.exe unchanged'
    Assert-Test ((Get-FileHash -LiteralPath (Join-Path $taskApp 'Plugins\docking.dll')).Hash -eq $taskDllHash) 'Original docking.dll unchanged'
    Assert-Test ((Get-ItemProperty -LiteralPath $taskKey).DisplayVersion -eq '1.1.0') 'Existing installer identity upgraded in place'
    $taskLink = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $taskGroup 'QIP Test (isolated).lnk'))
    Assert-Test ($taskLink.TargetPath -eq (Join-Path $taskApp 'QipLauncher.exe') -and $taskLink.WorkingDirectory -eq $taskApp) 'Shortcut uses Launcher and original working directory'
    Assert-Test (Test-Path -LiteralPath (Join-Path $taskGroup 'Настройки обновлений.lnk')) 'Updater settings shortcut exists'
    $taskSettings = Get-Content -LiteralPath (Join-Path $taskAgentState 'settings.json') -Raw | ConvertFrom-Json
    Assert-Test ($taskSettings.check_automatically -and -not $taskSettings.install_after_exit) 'Auto-check enabled and automatic installation disabled by default'
    $taskBackups = @(Get-ChildItem -LiteralPath (Join-Path $taskAgentState 'backups') -Directory)
    Assert-Test ($taskBackups.Count -eq 1) 'Manual update created a complete backup before replacing files'
    Assert-Test (Same-Hashes $taskBefore (User-Hashes (Join-Path $taskBackups[0].FullName 'application\Users'))) 'Backup contains exact private profile bytes'
    $taskNewLog = Get-Content -LiteralPath (Join-Path $taskWork 'install-1.1.0.log') -Raw
    Assert-Test ($taskNewLog -match 'User privileges: None' -and $taskNewLog -match 'Administrative install mode: No') 'Upgrade needs no administrator privileges'
    $taskUninstaller = Start-Process -FilePath (Join-Path $taskApp 'Uninstall\unins000.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
    Assert-Test ($taskUninstaller.ExitCode -eq 0) '1.1.0 uninstaller exit 0'
    Assert-Test (Same-Hashes $taskBefore (User-Hashes $taskUsers)) 'Default 1.1.0 uninstall preserves private profiles'
    Assert-Test (-not (Test-Path -LiteralPath $taskKey)) 'Isolated installer registration removed'
    Assert-Test (-not (Test-Path -LiteralPath $taskGroup)) 'Isolated Start-menu shortcuts removed'
} finally {
    # Preserve all generated private backups instead of recursively deleting user data.
    if (Test-Path -LiteralPath $taskAgentState) { Move-Item -LiteralPath $taskAgentState -Destination $taskNewAgentState }
    if (Test-Path -LiteralPath $taskOriginalAgentState) { Move-Item -LiteralPath $taskOriginalAgentState -Destination $taskAgentState }
    $taskResults | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskWork 'results.json') -Encoding utf8
}
