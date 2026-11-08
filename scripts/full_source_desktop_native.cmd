@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "DESKTOP_WORK=C:\Users\18268\AppData\Local\Temp\oo10-full-src"
set "DESKTOP_SDK=%DESKTOP_WORK%\DesktopEditors\desktop-sdk\ChromiumBasedEditors\lib"
set "DESKTOP_NAME=%~1"
set "DESKTOP_RUN=%~2"
set "DESKTOP_JOBS=3"
if not "%~3"=="" set "DESKTOP_JOBS=%~3"
if "%DESKTOP_RUN%"=="" exit /b 2
set "DESKTOP_PRO="
if "%DESKTOP_NAME%"=="ascdocumentscore" set "DESKTOP_PRO=ascdocumentscore.pro"
if "%DESKTOP_NAME%"=="editors_helper" set "DESKTOP_PRO=ascdocumentscore_helper.pro"
if "%DESKTOP_PRO%"=="" exit /b 2
set "DESKTOP_LOG=%DESKTOP_WORK%\desktop-build-evidence\native-%DESKTOP_NAME%-%DESKTOP_RUN%"
if exist "%DESKTOP_LOG%" exit /b 3
mkdir "%DESKTOP_LOG%"
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207 >"%DESKTOP_LOG%\vcvars.log" 2>&1
if errorlevel 1 exit /b 4
if not "%VCToolsVersion%"=="14.44.35207" exit /b 4
set "DESTDIR_BUILD_OVERRIDE="
set "DEST_MAKEFILE_NAME="
set "OO_BRANDING="
set "WINDOWS_VERSION_XP="
set "ADDITIONAL_INCLUDE_PATH="
set "QMAKEFEATURES="
set "QMAKEPATH="
set "CL="
set "_CL_="
set "LINK="
set "_LINK_="
set "MAKEFLAGS="
set "PRODUCT_VERSION=10.0.0"
set "BUILD_NUMBER=0"
if not exist "%DESKTOP_WORK%\desktop-builds\%DESKTOP_NAME%" mkdir "%DESKTOP_WORK%\desktop-builds\%DESKTOP_NAME%"
pushd "%DESKTOP_WORK%\desktop-builds\%DESKTOP_NAME%"
if errorlevel 1 exit /b 5
"D:\Qt\6.11.1\msvc2022_64\bin\qmake.exe" -nocache -spec win32-msvc -o Makefile "%DESKTOP_SDK%\%DESKTOP_PRO%" "QMAKE_TARGET.arch=x86_64" "CONFIG-=debug" "CONFIG-=debug_and_release" "CONFIG-=debug_and_release_target" "CONFIG-=force_debug_info" "CONFIG-=separate_debug_info" "CONFIG-=core_debug" "CONFIG+=release" "CONFIG+=vs2022" "CONFIG+=desktop" "CONFIG+=disable_precompiled_header" "QMAKE_CXXFLAGS+=/utf-8" -after "OBJECTS_DIR=%DESKTOP_WORK%/obj/%DESKTOP_NAME%" >"%DESKTOP_LOG%\qmake.log" 2>&1
if errorlevel 1 goto fail
"D:\Qt\Tools\QtCreator\bin\jom\jom.exe" /J %DESKTOP_JOBS% /NOLOGO /F Makefile >"%DESKTOP_LOG%\build.log" 2>&1
if errorlevel 1 goto fail
popd
echo DESKTOP_BUILD_PASSED %DESKTOP_NAME%
exit /b 0
:fail
popd
echo DESKTOP_BUILD_FAILED %DESKTOP_NAME%
exit /b 1
