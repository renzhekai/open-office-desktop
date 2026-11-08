@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "SSL_ROOT=C:\Users\18268\AppData\Local\Temp\oo10-full-src"
set "SSL_PROJECT=D:\open office"
set "SSL_BASE=%SSL_ROOT%\DesktopEditors\core\Common\3dParty\openssl"
set "SSL_PERL=%SSL_ROOT%\tools\strawberry-perl-5.40.0.1\perl\bin\perl.exe"
set "SSL_LOG=%SSL_ROOT%\dependency-evidence\openssl-release-native-perl-retry"
if not exist "%SSL_PERL%" exit /b 2
if exist "%SSL_LOG%" exit /b 3
mkdir "%SSL_LOG%"
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207 >"%SSL_LOG%\vcvars.log" 2>&1
if errorlevel 1 exit /b 4
if not "%VCToolsVersion%"=="14.44.35207" exit /b 4
set "PATH=%SSL_ROOT%\tools\strawberry-perl-5.40.0.1\perl\bin;%PATH%"
set "CL="
set "_CL_="
set "LINK="
set "LC_ALL=C"
set "LANG=C"
"%SSL_PERL%" -V:osname -V:version >"%SSL_LOG%\perl-version.log" 2>&1
if errorlevel 1 exit /b 5
python -B "%SSL_PROJECT%\scripts\configure_full_source_openssl.py"
if errorlevel 1 exit /b 6
pushd "%SSL_BASE%\openssl"
if errorlevel 1 exit /b 7
"%SSL_PERL%" Configure VC-WIN64A no-shared no-asm enable-md2 --prefix=%SSL_BASE%\build\win_64 --openssldir=%SSL_BASE%\build\win_64 >"%SSL_LOG%\configure.log" 2>&1
if errorlevel 1 goto fail
nmake /NOLOGO build_libs >"%SSL_LOG%\build.log" 2>&1
if errorlevel 1 goto fail
nmake /NOLOGO install_dev >"%SSL_LOG%\install-dev.log" 2>&1
if errorlevel 1 goto fail
popd
pushd "%SSL_LOG%"
cl /nologo /O2 /MD /I"%SSL_BASE%\build\win_64\include" "%SSL_PROJECT%\scripts\smoke_full_source_openssl.c" /Fe:openssl-source-smoke.exe /link /LIBPATH:"%SSL_BASE%\build\win_64\lib" libssl.lib libcrypto.lib ws2_32.lib crypt32.lib advapi32.lib user32.lib gdi32.lib >smoke-build.log 2>&1
if errorlevel 1 goto fail
openssl-source-smoke.exe >smoke-run.log 2>&1
if errorlevel 1 goto fail
popd
echo OPENSSL_BUILD_AND_SMOKE_PASSED
exit /b 0
:fail
popd
echo OPENSSL_BUILD_FAILED
exit /b 1
