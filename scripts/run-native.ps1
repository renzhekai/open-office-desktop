param([string]$SelectFile)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot 'build\native\bin\OpenOffice.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Run scripts\build-native.ps1 first.' }
$previousPath = $env:PATH
try {
    $env:PATH = "D:\Qt\6.11.1\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;$previousPath"
    if ($SelectFile) { & $executable --select $SelectFile } else { & $executable }
    if ($LASTEXITCODE -ne 0) { throw "Application exited with code $LASTEXITCODE" }
} finally {
    $env:PATH = $previousPath
}
