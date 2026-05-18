@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%install-board-wired-ip.ps1" %*

if errorlevel 1 (
  echo.
  echo Board wired-IP service install failed. See the messages above.
  if "%RK_WIRED_NO_PAUSE%"=="" pause
  exit /b %errorlevel%
)

echo.
echo Board wired-IP service install finished.
if "%RK_WIRED_NO_PAUSE%"=="" pause
