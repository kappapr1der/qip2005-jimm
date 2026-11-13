param(
    [Parameter(Mandatory=$true)][ValidateSet('Prepare','StartClient','StartCheck','Status','Finish')][string]$Phase,
    [Parameter(Mandatory=$true)][string]$AuditDirectory,
    [Parameter(Mandatory=$true)][string]$Guard,
    [Parameter(Mandatory=$true)][string]$BaselineInstaller,
    [string]$WorkingApplication = "$env:LOCALAPPDATA\Programs\QIP2005"
)
$ErrorActionPreference='Stop'
$auditRoot=[IO.Path]::GetFullPath($AuditDirectory)
$auditApp=Join-Path $auditRoot 'QIP2005'
$auditShared=[IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'QIP2005\UpdateAgent'))
$auditSaved=Join-Path $auditRoot 'prior-agent-state'
$auditCurrent=Join-Path $auditRoot 'test-agent-state'
$auditRegistration='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{A94D4B7F-5D7B-4E78-BF72-C44A3AC98849}_is1'
function Assert-Exit { if($LASTEXITCODE -ne 0){throw 'Audit helper failed'} }
function Audit-Processes {
    return @(Get-CimInstance Win32_Process -Filter "Name='qip.exe' OR Name='QipUpdateAgent.exe' OR Name='QipLauncher.exe' OR Name='QipUpdateWorker.exe'")
}
function Audit-Hashes([string]$path){
    $result=@{}
    if(Test-Path -LiteralPath $path){foreach($file in Get-ChildItem -LiteralPath $path -File -Recurse){$result[[IO.Path]::GetRelativePath($path,$file.FullName)]=(Get-FileHash -LiteralPath $file.FullName).Hash}}
    return $result
}
function Assert-Same($before,$after){
    if($before.Count -ne $after.Count){throw 'Audit preservation count mismatch'}
    foreach($name in $before.Keys){if($before[$name] -ne $after[$name]){throw 'Audit preservation hash mismatch'}}
}
if($Phase -eq 'Prepare'){
    if(Test-Path -LiteralPath $auditRoot){throw 'New audit directory required'}
    if((Audit-Processes).Count){throw 'Close QIP and its helpers before the production-installer audit'}
    New-Item -ItemType Directory -Path $auditRoot|Out-Null
    & $Guard capture $WorkingApplication (Join-Path $auditRoot 'working-state'); Assert-Exit
    # Resolve and check both named move targets before touching shared state.
    if($auditShared -ne [IO.Path]::GetFullPath("$env:LOCALAPPDATA\QIP2005\UpdateAgent") -or
       -not $auditSaved.StartsWith($auditRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected audit move target'}
    if(Test-Path -LiteralPath $auditShared){
        if((Get-Item -LiteralPath $auditShared).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse point refused'}
        Audit-Hashes $auditShared | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $auditRoot 'prior-state-hashes.json') -Encoding UTF8
        Move-Item -LiteralPath $auditShared -Destination $auditSaved
    }
    New-Item -ItemType Directory -Path $auditShared -Force|Out-Null
    [IO.File]::WriteAllText((Join-Path $auditShared 'settings.json'),'{"check_automatically":true,"install_after_exit":true,"backup_after_exit":false,"external_history_directory":""}',[Text.UTF8Encoding]::new($false))
    $argsSetup=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/TASKS=""',('/GROUP="QIP live audit '+(Split-Path $auditRoot -Leaf)+'"'),('/DIR="'+$auditApp+'"'),('/LOG="'+(Join-Path $auditRoot 'baseline-install.log')+'"'))
    $setup=Start-Process -FilePath $BaselineInstaller -ArgumentList $argsSetup -WindowStyle Hidden -Wait -PassThru
    if($setup.ExitCode -ne 0){throw 'Baseline installation failed; run Finish to restore shared integration'}
    $profile=Join-Path $auditApp 'Users\audit-fixture'
    $history=Join-Path $profile 'History'
    New-Item -ItemType Directory -Path $history -Force|Out-Null
    [IO.File]::WriteAllBytes((Join-Path $history 'synthetic-history.txt'),[Text.Encoding]::UTF8.GetBytes("Только синтетическая история для аудита.`r`nБез пользовательских сообщений.`r`n"))
    [IO.File]::WriteAllText((Join-Path $profile 'profile.ini'),("[Main]`r`nStartup=0`r`nHistPath="+$history+"`r`nSaveHistMsg=Yes`r`n"),[Text.UTF8Encoding]::new($true))
    Audit-Hashes $history | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $auditRoot 'history-before.json') -Encoding UTF8
    [pscustomobject]@{baseline_version=(Get-Content (Join-Path $auditApp 'installed-version.json') -Raw|ConvertFrom-Json).version;prepared=$true;synthetic_data_only=$true}|ConvertTo-Json
} elseif($Phase -eq 'StartClient'){
    if((Audit-Processes|Where-Object ExecutablePath -eq (Join-Path $auditApp 'qip.exe')).Count){throw 'Test client already running'}
    Start-Process -FilePath (Join-Path $auditApp 'qip.exe') -WorkingDirectory $auditApp
    'Account-free test client started; no credentials supplied.'
} elseif($Phase -eq 'StartCheck'){
    if(-not (Audit-Processes|Where-Object ExecutablePath -eq (Join-Path $auditApp 'qip.exe')).Count){throw 'Start the account-free test client first'}
    Start-Process -FilePath (Join-Path $auditApp 'QipUpdateAgent.exe') -ArgumentList '--monitor' -WorkingDirectory $auditApp -WindowStyle Hidden
    'Unmodified baseline agent is checking the real public GitHub Release.'
} elseif($Phase -eq 'Status'){
    $phases=@()
    if(Test-Path (Join-Path $auditShared 'transactions')){foreach($entry in Get-ChildItem (Join-Path $auditShared 'transactions') -Directory){$phases+=Get-Content (Join-Path $entry.FullName 'phase') -Raw}}
    $client=@(Audit-Processes|Where-Object ExecutablePath -eq (Join-Path $auditApp 'qip.exe'))
    [pscustomobject]@{installed_version=(Get-Content (Join-Path $auditApp 'installed-version.json') -Raw|ConvertFrom-Json).version;test_qip_instances=$client.Count;phases=$phases;download_files=@(Get-ChildItem (Join-Path $auditShared 'downloads') -File -Recurse -ErrorAction SilentlyContinue|Select-Object Name,Length);log_codes=@(Get-Content (Join-Path $auditShared 'agent.log') -Tail 12 -ErrorAction SilentlyContinue)}|ConvertTo-Json -Depth 4
} elseif($Phase -eq 'Finish'){
    if((Audit-Processes|Where-Object { $_.ExecutablePath -and ($_.ExecutablePath.StartsWith($auditApp+'\',[StringComparison]::OrdinalIgnoreCase) -or $_.ExecutablePath.StartsWith($auditShared+'\',[StringComparison]::OrdinalIgnoreCase))}).Count){throw 'Test helpers or client still running; shared state not restored'}
    $historyBefore=Get-Content (Join-Path $auditRoot 'history-before.json') -Raw|ConvertFrom-Json -AsHashtable
    Assert-Same $historyBefore (Audit-Hashes (Join-Path $auditApp 'Users\audit-fixture\History'))
    $uninstall=Start-Process -FilePath (Join-Path $auditApp 'Uninstall\unins000.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
    if($uninstall.ExitCode -ne 0){throw 'Test uninstall failed; shared integration backup remains available'}
    Assert-Same $historyBefore (Audit-Hashes (Join-Path $auditApp 'Users\audit-fixture\History'))
    if(-not $auditCurrent.StartsWith($auditRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $auditCurrent)){throw 'Unexpected audit preservation target'}
    if(Test-Path -LiteralPath $auditShared){Move-Item -LiteralPath $auditShared -Destination $auditCurrent}
    if(Test-Path -LiteralPath $auditSaved){Move-Item -LiteralPath $auditSaved -Destination $auditShared}
    & $Guard restore $WorkingApplication (Join-Path $auditRoot 'working-state'); Assert-Exit
    if(Test-Path (Join-Path $auditRoot 'prior-state-hashes.json')){Assert-Same (Get-Content (Join-Path $auditRoot 'prior-state-hashes.json') -Raw|ConvertFrom-Json -AsHashtable) (Audit-Hashes $auditShared)}
    [pscustomobject]@{working_files_unchanged=$true;working_integration_restored=$true;working_agent_state_restored=$true;synthetic_history_survives_update_and_uninstall=$true}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $auditRoot 'preservation-result.json') -Encoding UTF8
    Get-Content (Join-Path $auditRoot 'preservation-result.json')
}
