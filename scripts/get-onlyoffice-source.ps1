# Initial source acquisition; existing targets require an inspected, scoped resume.
# Downloads source only; it does not install, build, run upstream code, or accept agreements.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceParent = [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'upstream'))
if (-not $sourceParent.StartsWith($projectRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The source target must be inside the project.'
}
if (Test-Path -LiteralPath $sourceParent) {
    throw "Source target already exists: $sourceParent. Nothing was overwritten. Ask the agent to inspect and resume the existing download."
}
Get-Command git -ErrorAction Stop | Out-Null
$lock = Get-Content -LiteralPath (Join-Path $projectRoot 'sources.lock.json') -Raw -Encoding utf8 | ConvertFrom-Json
# Keep the reviewed remotes/version explicit, even if someone edits the lock file.
$desktopCommit = 'daf8c6948148827201b2907ac1134d222fbfa7b4'
$buildCommit = '43a46b1016f48cfec1bb86f84f3cb9f2a60ecfda'
if ($lock.desktop_editors.commit -ne $desktopCommit -or $lock.build_tools.commit -ne $buildCommit) {
    throw 'The source lock changed. Review this acquisition script before proceeding.'
}
$desktopPath = Join-Path $sourceParent 'DesktopEditors'
$buildToolsPath = Join-Path $desktopPath 'build_tools'
New-Item -ItemType Directory -Path $sourceParent | Out-Null

& git clone --branch v10.0.0 --depth 1 --no-checkout https://github.com/ONLYOFFICE/DesktopEditors.git $desktopPath
if ($LASTEXITCODE -ne 0) { throw "DesktopEditors clone failed. Preserve the partial directory for inspection: $sourceParent" }
$actualDesktop = & git -C $desktopPath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualDesktop.Trim() -ne $desktopCommit) { throw 'DesktopEditors tag does not match the reviewed commit. Stopping.' }
& git -C $desktopPath checkout --detach $desktopCommit
if ($LASTEXITCODE -ne 0) { throw 'DesktopEditors checkout failed.' }
& git -C $desktopPath submodule update --init --recursive --depth 1
if ($LASTEXITCODE -ne 0) { throw 'Submodule acquisition failed. Preserve the partial tree; do not delete or restart blindly.' }

& git clone --filter=blob:none --no-checkout https://github.com/ONLYOFFICE/build_tools.git $buildToolsPath
if ($LASTEXITCODE -ne 0) { throw 'build_tools clone failed.' }
& git -C $buildToolsPath checkout --detach $buildCommit
if ($LASTEXITCODE -ne 0) { throw 'build_tools checkout failed.' }
$actualBuild = & git -C $buildToolsPath rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualBuild.Trim() -ne $buildCommit) { throw 'build_tools revision verification failed.' }

Write-Output "Source repositories acquired at: $sourceParent"
Write-Output 'This is NOT a complete build dependency download or an integrated editor.'
Write-Output 'CEF/V8, toolchain compatibility, generated JS resources and Windows build dependencies still need inspection.'
Write-Output 'No build, installation, application launch, system configuration change, or license acceptance was performed.'
