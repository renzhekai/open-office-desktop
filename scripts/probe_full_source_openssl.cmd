@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "SSL_ROOT=C:\Users\18268\AppData\Local\Temp\oo10-full-src"
set "SSL_BASE=%SSL_ROOT%\DesktopEditors\core\Common\3dParty\openssl"
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207 >"%SSL_ROOT%\dependency-evidence\openssl-vcvars.log" 2>&1
if errorlevel 1 exit /b 2
pushd "%SSL_BASE%\openssl"
if errorlevel 1 exit /b 3
"C:\Program Files\Git\usr\bin\perl.exe" Configure VC-WIN64A no-shared no-asm enable-md2 --prefix=%SSL_BASE%\build\win_64 --openssldir=%SSL_BASE%\build\win_64 >"%SSL_ROOT%\dependency-evidence\openssl-git-perl-configure.log" 2>&1
set "SSL_RESULT=%ERRORLEVEL%"
popd
exit /b %SSL_RESULT%
