param([string]$Destination = '')
$ErrorActionPreference = 'Stop'
if (-not $Destination) { $Destination = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\toolchain' }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$taskArchive = Join-Path $Destination 'w64devkit-x64-2.9.1.7z.exe'
Invoke-WebRequest -Uri 'https://github.com/skeeto/w64devkit/releases/download/v2.9.1/w64devkit-x64-2.9.1.7z.exe' -OutFile $taskArchive
if ((Get-FileHash -LiteralPath $taskArchive).Hash -ne '9208C19755CD4964B7915B9AFCF02C66D493A4C870C4B3E83F6C538D9C1237A5') { throw 'Toolchain archive hash mismatch.' }
# Read the self-extracting archive as data; do not execute its stub.
& 7z.exe x $taskArchive ('-o'+$Destination) -y
if ($LASTEXITCODE -ne 0) { throw 'Toolchain extraction failed.' }
$taskBin = Join-Path $Destination 'w64devkit\bin'
Write-Output $taskBin
