@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "KS_ROOT=C:\Users\18268\AppData\Local\Temp\oo10-full-src"
set "KS_CORE=%KS_ROOT%\DesktopEditors\core"
set "KS_LOG=%KS_ROOT%\core-evidence\kernel-smoke"
if exist "%KS_LOG%" exit /b 3
mkdir "%KS_LOG%"
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207 >"%KS_LOG%\vcvars.log" 2>&1
if errorlevel 1 exit /b 4
set "PATH=%KS_CORE%\build\lib\win_64;%KS_CORE%\Common\3dParty\icu\win_64\build;%PATH%"
pushd "%KS_LOG%"
if errorlevel 1 exit /b 5
cl /nologo /O2 /MD /EHsc /std:c++17 /utf-8 /I"%KS_CORE%" "D:\open office\scripts\smoke_full_source_kernel.cpp" /Fe:kernel-source-smoke.exe /link /LIBPATH:"%KS_CORE%\build\lib\win_64" kernel.lib UnicodeConverter.lib >build.log 2>&1
if errorlevel 1 goto fail
kernel-source-smoke.exe >run.log 2>&1
set "KS_RESULT=%ERRORLEVEL%"
popd
exit /b %KS_RESULT%
:fail
popd
exit /b 1
