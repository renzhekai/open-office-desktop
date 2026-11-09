@echo off
rem Lists every runnable build in build\, newest first.
python -I "%~dp0scripts\open_runtime.py" --list
pause
