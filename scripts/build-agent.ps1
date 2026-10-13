param([string]$ToolchainBin = '', [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path -Parent $PSScriptRoot
if (-not $ToolchainBin) {
    $taskCommand = Get-Command g++.exe -ErrorAction SilentlyContinue
    if (-not $taskCommand) { throw 'Pass -ToolchainBin with a MinGW-w64 x64 compiler directory.' }
    $ToolchainBin = Split-Path -Parent $taskCommand.Source
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $taskProject 'build\agent' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$taskOldPath = $env:PATH
$taskCompiler = Join-Path $ToolchainBin 'g++.exe'
$taskWindres = Join-Path $ToolchainBin 'windres.exe'
try {
    $env:PATH = $ToolchainBin + ';' + $taskOldPath
    if ((& $taskCompiler -dumpmachine).Trim() -ne 'x86_64-w64-mingw32') { throw 'Expected x64 MinGW-w64 target.' }
    $taskVendor = Join-Path $taskProject 'updater\vendor\json.hpp'
    if ((Get-FileHash -LiteralPath $taskVendor).Hash -ne 'AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63') { throw 'Vendored JSON header hash mismatch.' }
    Push-Location (Join-Path $taskProject 'updater\src')
    try {
        & $taskWindres 'agent.rc' '-O' 'coff' '-o' (Join-Path $OutputDirectory 'agent-resource.o')
        if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
        & $taskCompiler '-std=c++20' '-Os' '-Wall' '-Wextra' '-Wno-misleading-indentation' '-D_WIN32_WINNT=0x0A00' '-DUNICODE' '-D_UNICODE' '-static' '-municode' '-mwindows' '-s' '-Wl,--nxcompat,--dynamicbase' 'main.cpp' (Join-Path $OutputDirectory 'agent-resource.o') '-o' (Join-Path $OutputDirectory 'QipUpdateAgent.exe') '-lwinhttp' '-lbcrypt' '-lcrypt32' '-lshell32' '-lole32' '-ladvapi32' '-luser32'
        if ($LASTEXITCODE -ne 0) { throw 'Native agent compilation failed.' }
    } finally { Pop-Location }
    Copy-Item -LiteralPath (Join-Path $OutputDirectory 'QipUpdateAgent.exe') -Destination (Join-Path $OutputDirectory 'QipLauncher.exe')
    $taskVersion = (Get-Content -LiteralPath (Join-Path $taskProject 'VERSION') -Raw).Trim()
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'installed-version.json'),('{"version":"'+$taskVersion+'","repository":"kappapr1der/qip2005-jimm"}'),[Text.UTF8Encoding]::new($false))
    $taskLicenseDir = Join-Path $OutputDirectory 'Licenses'
    New-Item -ItemType Directory -Path $taskLicenseDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $taskProject 'LICENSE') -Destination (Join-Path $taskLicenseDir 'QIP-Update-Agent-MIT.txt')
    Copy-Item -LiteralPath (Join-Path $taskProject 'updater\vendor\LICENSE.nlohmann-json') -Destination (Join-Path $taskLicenseDir 'nlohmann-json-MIT.txt')
    Get-ChildItem -LiteralPath (Join-Path $taskProject 'docs\licenses') -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $taskLicenseDir }
    Write-Output "Native agent built: $OutputDirectory"
} finally { $env:PATH = $taskOldPath }
