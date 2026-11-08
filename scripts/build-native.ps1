param(
    [string]$QtRoot = 'D:\Qt\6.11.1\mingw_64',
    [string]$MinGWRoot = 'D:\Qt\Tools\mingw1310_64',
    [string]$Ninja = 'D:\Qt\Tools\Ninja\ninja.exe',
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $projectRoot 'build\native'
$previousPath = $env:PATH
try {
    $env:PATH = "$QtRoot\bin;$MinGWRoot\bin;$previousPath"
    & cmake -S $projectRoot -B $buildPath -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$MinGWRoot\bin\g++.exe" `
        "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $buildPath --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
    if (-not $SkipTests) {
        & ctest --test-dir $buildPath --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
    }
    Write-Output "Built: $buildPath\bin\OpenOffice.exe"
} finally {
    $env:PATH = $previousPath
}
