@echo off
rem Starts the most recently built Open Office in build\ (it may still be an unverified candidate).
python -I "%~dp0scripts\open_runtime.py" --newest
if errorlevel 1 pause
