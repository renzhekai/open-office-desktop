param([string]$OutputDirectory = 'dist\OpenOffice-0.1.0')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
if (-not $outputPath.StartsWith($projectRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package output must be a new directory inside the workspace.'
}
if (Test-Path -LiteralPath $outputPath) { throw 'Output directory already exists. Use a new directory to preserve existing files.' }
$binary = Join-Path $projectRoot 'build\native\bin\OpenOffice.exe'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Run scripts\build-native.ps1 first.' }
$previousPath = $env:PATH
try {
    $env:PATH = "D:\Qt\6.11.1\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;$previousPath"
    New-Item -ItemType Directory -Path $outputPath | Out-Null
    Copy-Item -LiteralPath $binary -Destination (Join-Path $outputPath 'OpenOffice.exe')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -Destination $outputPath
    & 'D:\Qt\6.11.1\mingw_64\bin\windeployqt.exe' --release --no-translations `
        --no-system-d3d-compiler --no-opengl-sw --dir $outputPath (Join-Path $outputPath 'OpenOffice.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Local Qt dependency collection failed.' }
    Write-Output "Local development package: $outputPath"
    Write-Output 'This is a native UI prototype without the ONLYOFFICE engine. It is not a public release package.'
} finally {
    $env:PATH = $previousPath
}
