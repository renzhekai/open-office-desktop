@echo off
setlocal DisableDelayedExpansion
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207
if not "%errorlevel%"=="0" exit /b %errorlevel%
if not "%VCToolsVersion%"=="14.44.35207" exit /b 3
set "PATH=%~dp0..\dependencies\onlyoffice-v10.0.0\runtime;%~dp0..\dependencies\onlyoffice-v10.0.0\runtime\converter;%~dp0..\build\native-onlyoffice-qt6\bin;D:\Qt\6.11.1\msvc2022_64\bin;D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\Ninja;%PATH%"
"D:\Qt\Tools\CMake_64\bin\cmake.exe" -S "%~dp0.." -B "%~dp0..\build\native-engine" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DBUILD_TESTING=ON -DOPEN_OFFICE_WITH_ONLYOFFICE=ON -DCMAKE_PREFIX_PATH=D:/Qt/6.11.1/msvc2022_64 -DCMAKE_MAKE_PROGRAM=D:/Qt/Tools/Ninja/ninja.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
"D:\Qt\Tools\CMake_64\bin\cmake.exe" --build "%~dp0..\build\native-engine" --parallel 4
if not "%errorlevel%"=="0" exit /b %errorlevel%
"D:\Qt\Tools\CMake_64\bin\ctest.exe" --test-dir "%~dp0..\build\native-engine" --output-on-failure
exit /b %errorlevel%
