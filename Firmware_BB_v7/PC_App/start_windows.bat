@echo off
setlocal
cd /d "%~dp0"
if not exist ".venv\Scripts\python.exe" goto setup
".venv\Scripts\python.exe" -c "import serial, matplotlib, tkinter" >nul 2>nul
if errorlevel 1 goto setup
goto run
:setup
call "%~dp0setup_windows.bat"
if errorlevel 1 exit /b 1
:run
".venv\Scripts\python.exe" "psu_tuner.py"
if errorlevel 1 pause
endlocal
