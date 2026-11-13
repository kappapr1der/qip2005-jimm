param(
    [Parameter(Mandatory=$true)][string]$ISCC,
    [string]$Python = 'python',
    [string]$PayloadDirectory = '',
    [string]$OutputDirectory = '',
    [string]$SigningKeyDpapi = '',
    [string]$AgentDirectory = '',
    [switch]$CompileOnly
)
$ErrorActionPreference = 'Stop'
if (-not $CompileOnly -and -not $SigningKeyDpapi) { throw 'An explicit local DPAPI signing-key path is required. Use -CompileOnly for unsigned local verification.' }
$taskProject = Split-Path -Parent $PSScriptRoot
if (-not $PayloadDirectory) { $PayloadDirectory = Join-Path $taskProject 'private\payload' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $taskProject 'dist' }
if (-not $AgentDirectory) { $AgentDirectory = Join-Path $taskProject 'build\agent' }
$AgentDirectory = (Resolve-Path -LiteralPath $AgentDirectory).Path
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
& $taskCompiler ('/DPayloadDir='+$PayloadDirectory) ('/DAgentDir='+$AgentDirectory) ('/DOutputPath='+$OutputDirectory) (Join-Path $taskProject 'installer\QIP2005.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
if ($CompileOnly) {
    Write-Output 'Unsigned CI build compiled for verification only; it must not be published as an update.'
    return
}
$taskSignArgs = @((Join-Path $PSScriptRoot 'sign_release.py'),'--directory',$OutputDirectory,'--payload',$PayloadDirectory,'--agent-directory',$AgentDirectory)
if ($SigningKeyDpapi) { $taskSignArgs += @('--key-dpapi',$SigningKeyDpapi) }
& $Python @taskSignArgs
if ($LASTEXITCODE -ne 0) { throw 'Manifest signing failed; do not publish the unsigned build.' }
& $Python (Join-Path $PSScriptRoot 'verify_release.py') $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Release verification failed.' }
