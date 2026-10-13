param(
    [Parameter(Mandatory=$true)][string]$ISCC,
    [string]$Python = 'python',
    [string]$PayloadDirectory = '',
    [string]$OutputDirectory = '',
    [string]$SigningKeyDpapi = ''
)
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path -Parent $PSScriptRoot
if (-not $PayloadDirectory) { $PayloadDirectory = Join-Path $taskProject 'private\payload' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $taskProject 'dist' }
$PayloadDirectory = (Resolve-Path -LiteralPath $PayloadDirectory).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
& $Python (Join-Path $PSScriptRoot 'verify_payload.py') $PayloadDirectory
if ($LASTEXITCODE -ne 0) { throw 'Payload verification failed.' }
& $Python (Join-Path $PSScriptRoot 'extract_client_icon.py') (Join-Path $PayloadDirectory 'qip.exe') (Join-Path $taskProject 'private\qip.ico')
if ($LASTEXITCODE -ne 0) { throw 'Client icon extraction failed.' }
$taskCompiler = (Resolve-Path -LiteralPath $ISCC).Path
$taskSignature = Get-AuthenticodeSignature -LiteralPath $taskCompiler
if ($taskSignature.Status -ne 'Valid' -or $taskSignature.SignerCertificate.Subject -notmatch 'Pyrsys B\.V\.') { throw 'Official Inno Setup compiler signature was not verified.' }
& $taskCompiler ('/DPayloadDir='+$PayloadDirectory) ('/DOutputPath='+$OutputDirectory) (Join-Path $taskProject 'installer\QIP2005.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
$taskSignArgs = @((Join-Path $PSScriptRoot 'sign_release.py'),'--directory',$OutputDirectory,'--payload',$PayloadDirectory)
if ($SigningKeyDpapi) { $taskSignArgs += @('--key-dpapi',$SigningKeyDpapi) }
& $Python @taskSignArgs
if ($LASTEXITCODE -ne 0) { throw 'Manifest signing failed; do not publish the unsigned build.' }
& $Python (Join-Path $PSScriptRoot 'verify_release.py') $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Release verification failed.' }
