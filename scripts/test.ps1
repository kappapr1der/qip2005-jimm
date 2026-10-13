param([string]$ToolchainBin = '', [string]$Python = 'python', [string]$RealBaseline = '')
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path -Parent $PSScriptRoot
if (-not $ToolchainBin) { $ToolchainBin = Split-Path -Parent (Get-Command g++.exe -ErrorAction Stop).Source }
$taskOutput = Join-Path $taskProject 'build\tests'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskOldPath = $env:PATH
try {
    $env:PATH = $ToolchainBin + ';' + $taskOldPath
    & (Join-Path $ToolchainBin 'g++.exe') '-std=c++20' '-Os' '-D_WIN32_WINNT=0x0A00' '-DUNICODE' '-D_UNICODE' '-static' '-municode' (Join-Path $taskProject 'tests\native_harness.cpp') '-o' (Join-Path $taskOutput 'native-harness.exe') '-lwinhttp' '-lbcrypt' '-lcrypt32' '-lshell32' '-lole32' '-ladvapi32' '-luser32'
    if ($LASTEXITCODE -ne 0) { throw 'Native test harness compilation failed.' }
    & (Join-Path $ToolchainBin 'g++.exe') '-Os' '-static' '-municode' '-mwindows' (Join-Path $taskProject 'tests\slow_client.cpp') '-o' (Join-Path $taskOutput 'qip.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Controlled process fixture compilation failed.' }
    $taskArguments = @((Join-Path $taskProject 'tests\test_updater.py'),'--report',(Join-Path $taskOutput 'results.json'))
    if ($RealBaseline) { $taskArguments += @('--real-baseline',$RealBaseline) }
    & $Python @taskArguments
    if ($LASTEXITCODE -ne 0) { throw 'Updater tests failed.' }
} finally { $env:PATH = $taskOldPath }
