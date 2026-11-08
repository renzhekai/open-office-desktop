@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "CORE_WORK=C:\Users\18268\AppData\Local\Temp\oo10-full-src"
set "CORE_SOURCE=%CORE_WORK%\DesktopEditors\core"
set "CORE_NAME=%~1"
set "CORE_RUN=%~2"
set "CORE_JOBS=4"
if not "%~3"=="" set "CORE_JOBS=%~3"
if "%CORE_RUN%"=="" exit /b 2
set "CORE_PRO="
if "%CORE_NAME%"=="unicode" set "CORE_PRO=UnicodeConverter\UnicodeConverter.pro"
if "%CORE_NAME%"=="kernel" set "CORE_PRO=Common\kernel.pro"
if "%CORE_NAME%"=="graphics" set "CORE_PRO=DesktopEditor\graphics\pro\graphics.pro"
if "%CORE_NAME%"=="starmath" set "CORE_PRO=OdfFile\Reader\Converter\StarMath2OOXML\StarMath2OOXML.pro"
if "%CORE_NAME%"=="signature" set "CORE_PRO=DesktopEditor\xmlsec\src\ooxmlsignature.pro"
if "%CORE_NAME%"=="cryptcli" set "CORE_PRO=OfficeCryptReader\ooxml_crypt\ooxml_crypt.pro"
if "%CORE_NAME%"=="network" set "CORE_PRO=Common\Network\network.pro"
if "%CORE_NAME%"=="docxformat" set "CORE_PRO=OOXML\Projects\Linux\DocxFormatLib\DocxFormatLib.pro"
if "%CORE_NAME%"=="pptxformat" set "CORE_PRO=OOXML\Projects\Linux\PPTXFormatLib\PPTXFormatLib.pro"
if "%CORE_NAME%"=="xlsbformat" set "CORE_PRO=OOXML\Projects\Linux\XlsbFormatLib\XlsbFormatLib.pro"
if "%CORE_NAME%"=="pdf" set "CORE_PRO=PdfFile\PdfFile.pro"
if "%CORE_NAME%"=="djvu" set "CORE_PRO=DjVuFile\DjVuFile.pro"
if "%CORE_NAME%"=="xps" set "CORE_PRO=XpsFile\XpsFile.pro"
if "%CORE_NAME%"=="ofd" set "CORE_PRO=OFDFile\OFDFile.pro"
if "%CORE_NAME%"=="html" set "CORE_PRO=HtmlFile2\HtmlFile2.pro"
if "%CORE_NAME%"=="fb2" set "CORE_PRO=Fb2File\Fb2File.pro"
if "%CORE_NAME%"=="epub" set "CORE_PRO=EpubFile\CEpubFile.pro"
if "%CORE_NAME%"=="hwp" set "CORE_PRO=HwpFile\HWPFile.pro"
if "%CORE_NAME%"=="iwork" set "CORE_PRO=Apple\IWork.pro"
if "%CORE_NAME%"=="docxrenderer" set "CORE_PRO=DocxRenderer\DocxRenderer.pro"
if "%CORE_NAME%"=="doctrenderer" set "CORE_PRO=DesktopEditor\doctrenderer\doctrenderer.pro"
if "%CORE_NAME%"=="docformat" set "CORE_PRO=MsBinaryFile\Projects\DocFormatLib\Linux\DocFormatLib.pro"
if "%CORE_NAME%"=="pptformat" set "CORE_PRO=MsBinaryFile\Projects\PPTFormatLib\Linux\PPTFormatLib.pro"
if "%CORE_NAME%"=="xlsformat" set "CORE_PRO=MsBinaryFile\Projects\XlsFormatLib\Linux\XlsFormatLib.pro"
if "%CORE_NAME%"=="vbaformat" set "CORE_PRO=MsBinaryFile\Projects\VbaFormatLib\Linux\VbaFormatLib.pro"
if "%CORE_NAME%"=="txtformat" set "CORE_PRO=TxtFile\Projects\Linux\TxtXmlFormatLib.pro"
if "%CORE_NAME%"=="rtfformat" set "CORE_PRO=RtfFile\Projects\Linux\RtfFormatLib.pro"
if "%CORE_NAME%"=="odfformat" set "CORE_PRO=OdfFile\Projects\Linux\OdfFormatLib.pro"
if "%CORE_NAME%"=="bindocument" set "CORE_PRO=OOXML\Projects\Linux\BinDocument\BinDocument.pro"
if "%CORE_NAME%"=="x2t" set "CORE_PRO=X2tConverter\build\Qt\X2tConverter.pro"
if "%CORE_NAME%"=="allfontsgen" set "CORE_PRO=DesktopEditor\AllFontsGen\AllFontsGen.pro"
if "%CORE_NAME%"=="allthemesgen" set "CORE_PRO=DesktopEditor\allthemesgen\allthemesgen.pro"
if "%CORE_NAME%"=="docbuilder" set "CORE_PRO=DesktopEditor\doctrenderer\app_builder\docbuilder.pro"
if "%CORE_NAME%"=="pluginsmanager" set "CORE_PRO=DesktopEditor\pluginsmanager\pluginsmanager.pro"
if "%CORE_PRO%"=="" exit /b 2
set "CORE_LOG=%CORE_WORK%\core-evidence\%CORE_NAME%-%CORE_RUN%"
if exist "%CORE_LOG%" exit /b 3
mkdir "%CORE_LOG%"
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44.35207 >"%CORE_LOG%\vcvars.log" 2>&1
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
if not exist "%CORE_WORK%\core-builds\%CORE_NAME%" mkdir "%CORE_WORK%\core-builds\%CORE_NAME%"
pushd "%CORE_WORK%\core-builds\%CORE_NAME%"
if errorlevel 1 exit /b 5
"D:\Qt\6.11.1\msvc2022_64\bin\qmake.exe" -nocache -spec win32-msvc -o Makefile "%CORE_SOURCE%\%CORE_PRO%" "QMAKE_TARGET.arch=x86_64" "CONFIG-=debug" "CONFIG-=debug_and_release" "CONFIG-=debug_and_release_target" "CONFIG-=force_debug_info" "CONFIG-=separate_debug_info" "CONFIG-=core_debug" "CONFIG+=release" "CONFIG+=vs2022" "CONFIG+=desktop" "CONFIG+=disable_precompiled_header" "QMAKE_CXXFLAGS+=/utf-8" -after "OBJECTS_DIR=%CORE_WORK%/obj/%CORE_NAME%" >"%CORE_LOG%\qmake.log" 2>&1
if errorlevel 1 goto fail
rem Keep object_parallel_to_source: libxml xmlwriter.c and our xmlwriter.cpp must remain distinct.
"D:\Qt\Tools\QtCreator\bin\jom\jom.exe" /J %CORE_JOBS% /NOLOGO /F Makefile >"%CORE_LOG%\build.log" 2>&1
if errorlevel 1 goto fail
popd
echo CORE_BUILD_PASSED %CORE_NAME%
exit /b 0
:fail
popd
echo CORE_BUILD_FAILED %CORE_NAME%
exit /b 1
