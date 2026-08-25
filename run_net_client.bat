@echo off
setlocal
cd /d "%~dp0"

set "EXE=%~dp0cpp_core\build\Release\tactics_net_client.exe"

if not exist "%EXE%" (
  echo tactics_net_client.exe not found. Building...
  call "%~dp0build_standalone.bat"
  if errorlevel 1 exit /b 1
)
if not exist "%EXE%" (
  echo Failed to produce "%EXE%"
  exit /b 1
)

echo Join client. Start run_net_server.bat first. Type help then quit.
echo Extra args: --host 127.0.0.1 --port 8788 --seat 2
"%EXE%" %*
echo NET_CLIENT_EXIT=%ERRORLEVEL%
exit /b %ERRORLEVEL%
