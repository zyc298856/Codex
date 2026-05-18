@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%rk3588-wired-connect.ps1" %*

if errorlevel 1 (
  echo.
  echo Wired connection check failed. See the messages above.
  if "%RK_WIRED_NO_PAUSE%"=="" pause
  exit /b %errorlevel%
)

echo.
echo Wired connection check passed.
if "%RK_WIRED_NO_PAUSE%"=="" pause
