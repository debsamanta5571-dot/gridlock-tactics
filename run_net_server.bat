@echo off
setlocal
cd /d "%~dp0"

if not exist "%~dp0tactics_net_server.exe" if not exist "%~dp0cpp_core\build\Release\tactics_net_server.exe" (
  echo tactics_net_server.exe not found. Building...
  call "%~dp0build_standalone.bat"
  if errorlevel 1 exit /b 1
)
if exist "%~dp0tactics_net_server.exe" (
  set "EXE=%~dp0tactics_net_server.exe"
) else (
  set "EXE=%~dp0cpp_core\build\Release\tactics_net_server.exe"
)
if not exist "%EXE%" (
  echo Failed to produce tactics_net_server.exe
  exit /b 1
)

set "CONTENT=%~dp0TacticsGameUnreal 5.8\Content"
if not exist "%CONTENT%\TacticsData\ability_catalog.json" set "CONTENT=%~dp0Content"
if not exist "%CONTENT%\TacticsData\ability_catalog.json" (
  echo Missing card data at "%CONTENT%\TacticsData\ability_catalog.json"
  echo Need TacticsGameUnreal 5.8\Content or a Content folder next to this bat.
  exit /b 1
)

echo.
echo Headless host. Join with run_net_client.bat or Unreal Join ws://127.0.0.1:8788/
echo Extra args: --public  --port N  --token SECRET
echo.

"%EXE%" --content "%CONTENT%" %*
echo NET_SERVER_EXIT=%ERRORLEVEL%
exit /b %ERRORLEVEL%
