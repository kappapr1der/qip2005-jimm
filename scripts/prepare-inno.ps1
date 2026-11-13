param([string]$Directory = '')
$ErrorActionPreference='Stop'
if(-not $Directory){$Directory=Join-Path (Split-Path -Parent $PSScriptRoot) 'build\inno'}
$taskDirectory=[IO.Path]::GetFullPath($Directory)
New-Item -ItemType Directory -Path $taskDirectory -Force|Out-Null
$taskBootstrap=Join-Path $taskDirectory 'inno-setup.exe'
Invoke-WebRequest -Uri 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe' -OutFile $taskBootstrap
$taskSignature=Get-AuthenticodeSignature -LiteralPath $taskBootstrap
if($taskSignature.Status -ne 'Valid' -or $taskSignature.SignerCertificate.Subject -notmatch 'Pyrsys B\.V\.'){throw 'Official Inno compiler signature mismatch'}
$taskProcess=Start-Process -FilePath $taskBootstrap -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER','/PORTABLE=1','/NOICONS','/TASKS=',('/DIR="'+(Join-Path $taskDirectory 'compiler')+'"')) -WindowStyle Hidden -Wait -PassThru
if($taskProcess.ExitCode -ne 0){throw 'Inno compiler preparation failed'}
Write-Output (Join-Path $taskDirectory 'compiler\ISCC.exe')
