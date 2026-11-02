@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 (
    echo Python launcher 'py' not found. Install Python 3 from python.org.
    pause
    exit /b 1
)
py -3 "%~dp0setup_app.py"
if errorlevel 1 (
    echo Dependency setup failed. See error message above.
    pause
    exit /b 1
)
echo Setup completed successfully.
endlocal
