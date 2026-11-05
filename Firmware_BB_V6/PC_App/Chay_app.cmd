@echo off
setlocal
cd /d "%~dp0"
if exist "dist\BB_V6_Studio_v6_28.exe" (
  start "" "dist\BB_V6_Studio_v6_28.exe" %*
  exit /b
)
if exist "dist\BB_V6_Studio_v6_19.exe" (
  start "" "dist\BB_V6_Studio_v6_19.exe" %*
  exit /b
)
if exist "dist\BB_V6_Studio_v6_13_gain100_duty.exe" (
  start "" "dist\BB_V6_Studio_v6_13_gain100_duty.exe" %*
  exit /b
)
if exist "dist\BB_V6_Studio_v6_13_gain100.exe" (
  start "" "dist\BB_V6_Studio_v6_13_gain100.exe" %*
  exit /b
)
if exist "dist\BB_V6_Studio_v6_13.exe" (
  start "" "dist\BB_V6_Studio_v6_13.exe" %*
  exit /b
)
if exist "dist\BB_V6_Studio_v6_9.exe" (
  start "" "dist\BB_V6_Studio_v6_9.exe" %*
  exit /b
)
python app.py %*
if errorlevel 1 pause
