@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "ICU_ROOT=C:\Users\18268\AppData\Local\Temp\oo10-full-src"
set "ICU_SOURCE=%ICU_ROOT%\DesktopEditors\core\Common\3dParty\icu\icu"
set "ICU_LOG=%ICU_ROOT%\dependency-evidence\icu-release-desktop-target"
if not exist "%ICU_SOURCE%\source\allinone\allinone.sln" exit /b 2
if exist "%ICU_LOG%" exit /b 3
mkdir "%ICU_LOG%"
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207 >"%ICU_LOG%\vcvars.log" 2>&1
if errorlevel 1 exit /b 4
if not "%VCToolsVersion%"=="14.44.35207" exit /b 4
set "CL="
set "_CL_="
set "LINK="
set "MSBUILDDISABLENODEREUSE=1"
set "ICU_SDK=%WindowsSDKVersion:\=%"
rem Use the installed v145 MSBuild target definitions, but explicitly select the existing 14.44 compiler.
"D:\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "%ICU_SOURCE%\source\allinone\allinone.sln" /t:makedata /m:1 /nr:false /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /p:VCToolsVersion=14.44.35207 /p:WindowsTargetPlatformVersion=%ICU_SDK% >"%ICU_LOG%\msbuild.log" 2>&1
if errorlevel 1 exit /b 1
call "%~dp0rebuild_full_source_icu_data.cmd"
if errorlevel 1 exit /b 1
echo ICU_COMPILE_PASSED
exit /b 0
