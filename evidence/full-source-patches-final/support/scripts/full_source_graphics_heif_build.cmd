@echo off
setlocal
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207
if errorlevel 1 exit /b %errorlevel%
"C:\Users\18268\AppData\Local\Programs\Python\Python314\python.exe" "%~dp0full_source_graphics_heif_build.py" %*
exit /b %errorlevel%
