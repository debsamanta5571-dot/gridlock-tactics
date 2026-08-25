@echo off
setlocal
cd /d "%~dp0"

set "EXE=%~dp0cpp_core\build\Release\tactics_master_cli.exe"

if not exist "%EXE%" (
  echo tactics_master_cli.exe not found. Building...
  call "%~dp0build_standalone.bat"
  if errorlevel 1 exit /b 1
)
if not exist "%EXE%" (
  echo Failed to produce "%EXE%"
  exit /b 1
)

echo Interactive C++ match. Type help then quit.
"%EXE%" %*
echo CLI_EXIT=%ERRORLEVEL%
exit /b %ERRORLEVEL%
