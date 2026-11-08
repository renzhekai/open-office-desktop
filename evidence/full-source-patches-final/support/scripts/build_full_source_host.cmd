@echo off
setlocal EnableExtensions DisableDelayedExpansion
if "%~1"=="" exit /b 2
set "HOST_ROOT=%~dp0.."
set "HOST_LOG=%HOST_ROOT%\build\full-source\host-evidence\%~1"
if exist "%HOST_LOG%" exit /b 3
mkdir "%HOST_LOG%"
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207 >"%HOST_LOG%\vcvars.log" 2>&1
if errorlevel 1 exit /b 4
if not "%VCToolsVersion%"=="14.44.35207" exit /b 4
set "PATH=%HOST_ROOT%\build\full-source-run;%HOST_ROOT%\build\full-source-run\converter;%HOST_ROOT%\build\full-source-qt6\bin;D:\Qt\6.11.1\msvc2022_64\bin;D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\Ninja;%PATH%"
set "QT_PLUGIN_PATH=D:\Qt\6.11.1\msvc2022_64\plugins"
set "QT_QPA_PLATFORM_PLUGIN_PATH=D:\Qt\6.11.1\msvc2022_64\plugins\platforms"
"D:\Qt\Tools\CMake_64\bin\cmake.exe" -S "%HOST_ROOT%" -B "%HOST_ROOT%\build\full-source-host" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DBUILD_TESTING=ON -DOPEN_OFFICE_WITH_ONLYOFFICE=ON -DCMAKE_PREFIX_PATH=D:/Qt/6.11.1/msvc2022_64 -DCMAKE_MAKE_PROGRAM=D:/Qt/Tools/Ninja/ninja.exe "-DONLYOFFICE_SOURCE_ROOT=%HOST_ROOT%\build\full-source\DesktopEditors" "-DONLYOFFICE_RUNTIME_ROOT=%HOST_ROOT%\build\full-source-run" "-DONLYOFFICE_CORE_IMPORT_LIBRARY=%HOST_ROOT%\build\full-source\DesktopEditors\core\build\lib\win_64\ascdocumentscore.lib" "-DONLYOFFICE_QT_WRAPPER_BUILD=%HOST_ROOT%\build\full-source-qt6" >"%HOST_LOG%\configure.log" 2>&1
if errorlevel 1 goto fail
"D:\Qt\Tools\CMake_64\bin\cmake.exe" --build "%HOST_ROOT%\build\full-source-host" --parallel 4 >"%HOST_LOG%\build.log" 2>&1
if errorlevel 1 goto fail
"D:\Qt\Tools\CMake_64\bin\ctest.exe" --test-dir "%HOST_ROOT%\build\full-source-host" --output-on-failure >"%HOST_LOG%\ctest.log" 2>&1
if errorlevel 1 goto fail
echo SOURCE_HOST_BUILD_AND_TESTS_PASSED
exit /b 0
:fail
echo SOURCE_HOST_BUILD_OR_TESTS_FAILED
exit /b 1
