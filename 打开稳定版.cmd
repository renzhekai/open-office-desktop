@echo off
rem Starts the verified default entry (the build the installer packages).
python -I "%~dp0scripts\open_runtime.py" --default
if errorlevel 1 pause
