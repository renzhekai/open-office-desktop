@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "LEAF_ROOT=C:\Users\18268\AppData\Local\Temp\oo10-full-src"
if "%~1"=="" exit /b 2
set "LEAF_LOG=%LEAF_ROOT%\leaf-evidence\%~1"
set "LEAF_QMAKE=D:\Qt\6.11.1\msvc2022_64\bin\qmake.exe"
set "LEAF_TOOLS=D:\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64"
if not exist "%LEAF_ROOT%\worktrees.json" exit /b 2
if not exist "%LEAF_ROOT%\initial-toolchain-patch.json" exit /b 2
if exist "%LEAF_LOG%" exit /b 3
mkdir "%LEAF_LOG%"
if errorlevel 1 exit /b 3
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207 >"%LEAF_LOG%\vcvars.log" 2>&1
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
if "%~2"=="cfcpp" goto only_cfcpp
call :build cryptopp Common\3dParty\cryptopp\project\cryptopp.pro CryptoPPLib.lib
if errorlevel 1 exit /b 1
:only_cfcpp
call :build cfcpp Common\cfcpp\cfcpp.pro CompoundFileLib.lib
if errorlevel 1 exit /b 1
echo LEAF_BUILD_PASSED
exit /b 0

:build
echo BUILDING %~1
if not exist "%LEAF_ROOT%\leaf-builds\%~1" mkdir "%LEAF_ROOT%\leaf-builds\%~1"
if errorlevel 1 exit /b 1
pushd "%LEAF_ROOT%\leaf-builds\%~1"
if errorlevel 1 exit /b 1
"%LEAF_QMAKE%" -nocache -spec win32-msvc -o Makefile "%LEAF_ROOT%\DesktopEditors\core\%~2" "QMAKE_TARGET.arch=x86_64" "CONFIG-=debug" "CONFIG-=debug_and_release" "CONFIG-=debug_and_release_target" "CONFIG-=force_debug_info" "CONFIG-=separate_debug_info" "CONFIG-=core_debug" "CONFIG+=release" "CONFIG+=vs2022" "CONFIG+=desktop" "CONFIG+=disable_precompiled_header" "QMAKE_CXXFLAGS+=/utf-8" -after "CONFIG-=object_parallel_to_source" "OBJECTS_DIR=%LEAF_ROOT%/leaf-objects/%~1" >"%LEAF_LOG%\%~1-qmake.log" 2>&1
if errorlevel 1 goto build_failed
"%LEAF_TOOLS%\nmake.exe" /NOLOGO /F Makefile >"%LEAF_LOG%\%~1-nmake.log" 2>&1
if errorlevel 1 goto build_failed
if not exist "%LEAF_ROOT%\DesktopEditors\core\build\lib\win_64\%~3" goto build_failed
popd
echo BUILT %~3
exit /b 0

:build_failed
popd
echo FAILED %~1
exit /b 1
