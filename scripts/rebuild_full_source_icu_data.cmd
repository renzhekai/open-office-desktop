@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "ID_ROOT=C:\Users\18268\AppData\Local\Temp\oo10-full-src"
set "ID_SOURCE=%ID_ROOT%\DesktopEditors\core\Common\3dParty\icu\icu"
set "ID_LOG=%ID_ROOT%\dependency-evidence\icu-desktop-data-forced"
if exist "%ID_LOG%" exit /b 3
mkdir "%ID_LOG%"
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207 >"%ID_LOG%\vcvars.log" 2>&1
if errorlevel 1 exit /b 4
pushd "%ID_SOURCE%\source\data"
if errorlevel 1 exit /b 5
rem stubdata can overwrite bin64/icudt74.dll on incremental MSBuild; force final data packaging afterwards.
nmake /NOLOGO /A /f makedata.mak ICUMAKE="%ID_SOURCE%\source\data" CFG=x64\Release >"%ID_LOG%\nmake.log" 2>&1
set "ID_RESULT=%ERRORLEVEL%"
popd
exit /b %ID_RESULT%
