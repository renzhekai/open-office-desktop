@echo off
setlocal DisableDelayedExpansion
if not exist "D:\Qt\6.11.1\msvc2022_64\bin\qmake.exe" (
    echo Qt 6.11.1 MSVC2022 SDK is not prepared.
    exit /b 2
)
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207
if not "%errorlevel%"=="0" exit /b %errorlevel%
if not "%VCToolsVersion%"=="14.44.35207" exit /b 3
set "PATH=D:\Qt\6.11.1\msvc2022_64\bin;D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\Ninja;%PATH%"
"D:\Qt\Tools\CMake_64\bin\cmake.exe" -S "%~dp0.." -B "%~dp0..\build\native-msvc" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=D:/Qt/6.11.1/msvc2022_64 -DCMAKE_MAKE_PROGRAM=D:/Qt/Tools/Ninja/ninja.exe
if not "%errorlevel%"=="0" exit /b %errorlevel%
"D:\Qt\Tools\CMake_64\bin\cmake.exe" --build "%~dp0..\build\native-msvc" --parallel 4
if not "%errorlevel%"=="0" exit /b %errorlevel%
"D:\Qt\Tools\CMake_64\bin\ctest.exe" --test-dir "%~dp0..\build\native-msvc" --output-on-failure
exit /b %errorlevel%
