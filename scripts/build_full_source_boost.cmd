@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "BS_ROOT=C:\Users\18268\AppData\Local\Temp\oo10-full-src"
set "BS_PROJECT=D:\open office"
set "BS_SOURCE=%BS_ROOT%\DesktopEditors\core\Common\3dParty\boost\boost_1_72_0"
set "BS_LOG=%BS_ROOT%\dependency-evidence\boost-release-explicit-compiler"
if not exist "%BS_SOURCE%\tools\build\src\engine\config_toolset.bat" exit /b 2
if exist "%BS_LOG%" exit /b 3
mkdir "%BS_LOG%"
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207 >"%BS_LOG%\vcvars.log" 2>&1
if errorlevel 1 exit /b 4
if not "%VCToolsVersion%"=="14.44.35207" exit /b 4
set "CXX=cl"
set "CXXFLAGS="
set "CL="
set "_CL_="
set "LINK="
set "B2_TOOLSET_ROOT="
pushd "%BS_SOURCE%"
if errorlevel 1 exit /b 5
if exist b2.exe goto bootstrap_ready
call bootstrap.bat vc142 >"%BS_LOG%\bootstrap.log" 2>&1
if errorlevel 1 goto fail
:bootstrap_ready
python -B "%BS_PROJECT%\scripts\configure_full_source_boost.py" configure >"%BS_LOG%\configure.log" 2>&1
if errorlevel 1 goto fail
b2.exe -j8 --ignore-site-config --user-config="%BS_ROOT%\boost-empty-user-config.jam" --prefix=..\build\win_64 --build-dir="%BS_ROOT%\dependency-objects\boost" --with-filesystem --with-system --with-date_time --with-regex toolset=msvc-14.3 architecture=x86 address-model=64 variant=release threading=multi link=static runtime-link=shared --layout=versioned install >"%BS_LOG%\b2.log" 2>&1
if errorlevel 1 goto fail
python -B "%BS_PROJECT%\scripts\configure_full_source_boost.py" verify >"%BS_LOG%\verify.log" 2>&1
if errorlevel 1 goto fail
popd
echo BOOST_BUILD_PASSED
exit /b 0
:fail
popd
echo BOOST_BUILD_FAILED
exit /b 1
