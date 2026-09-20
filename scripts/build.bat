@echo off
setlocal
cd /d "%~dp0\.."
echo [Build] Building QuickDiskRescue...
python scripts\build.py %*
