param(
    [string]$SourcePackageUrl = 'https://github.com/kappapr1der/qip2005-jimm/releases/download/v1.1.1/QIP2005_clean.7z'
)
$ErrorActionPreference = 'Stop'
if (-not $SourcePackageUrl.StartsWith('https://')) { throw 'HTTPS source package required.' }
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskPrivate = Join-Path $taskRoot 'private'
New-Item -ItemType Directory -Path $taskPrivate -Force | Out-Null
$taskArchive = Join-Path $taskPrivate 'client.7z'
Invoke-WebRequest -Uri $SourcePackageUrl -OutFile $taskArchive
if ((Get-FileHash -LiteralPath $taskArchive).Hash -ne '0BCC0F2B0914F89AF68200FD811E8C96B9F28FCBDAC8FF034BDF02A81FD13A0C') {
    throw 'Original source package checksum mismatch.'
}
if (Test-Path -LiteralPath (Join-Path $taskPrivate 'payload')) { throw 'Existing payload will not be overwritten.' }
& 7z.exe x $taskArchive ('-o'+(Join-Path $taskPrivate 'unpacked')) -y | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Payload extraction failed.' }
$taskSource = [IO.Path]::GetFullPath((Join-Path $taskPrivate 'unpacked\QIP2005_clean'))
$taskExpected = [IO.Path]::GetFullPath((Join-Path $taskPrivate 'unpacked\QIP2005_clean'))
if ($taskSource -ne $taskExpected -or -not (Test-Path -LiteralPath $taskSource)) { throw 'Unexpected extraction target.' }
Move-Item -LiteralPath $taskSource -Destination (Join-Path $taskPrivate 'payload')
& python (Join-Path $PSScriptRoot 'verify_payload.py') (Join-Path $taskPrivate 'payload')
if ($LASTEXITCODE -ne 0) { throw 'Payload content check failed.' }
Invoke-WebRequest -Uri 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe' -OutFile (Join-Path $taskPrivate 'inno-setup.exe')
$taskSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path $taskPrivate 'inno-setup.exe')
if ($taskSignature.Status -ne 'Valid' -or $taskSignature.SignerCertificate.Subject -notmatch 'Pyrsys B\.V\.') { throw 'Official compiler signature mismatch.' }
$taskCompilerDir = Join-Path $taskPrivate 'inno'
$taskCompiler = Start-Process -FilePath (Join-Path $taskPrivate 'inno-setup.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER','/PORTABLE=1','/NOICONS','/TASKS=',('/DIR="'+$taskCompilerDir+'"')) -WindowStyle Hidden -Wait -PassThru
if ($taskCompiler.ExitCode -ne 0) { throw 'Compiler preparation failed.' }
