@echo off
setlocal
cd /d "%~dp0"

set "EXE=%~dp0cpp_core\build\Release\tactics_net_server.exe"
set "CONTENT=%~dp0TacticsGameUnreal 5.8\Content"

if not exist "%EXE%" (
  echo tactics_net_server.exe not found. Building...
  call "%~dp0build_standalone.bat"
  if errorlevel 1 exit /b 1
)
if not exist "%EXE%" (
  echo Failed to produce "%EXE%"
  exit /b 1
)
if not exist "%CONTENT%\TacticsData\ability_catalog.json" (
  echo Missing card data at "%CONTENT%\TacticsData\ability_catalog.json"
  echo --content must point at TacticsGameUnreal 5.8\Content
  exit /b 1
)

echo.
echo Standalone host. Unreal clients: Join, ws://127.0.0.1:8788/
echo Extra args: --public  --port N  --token SECRET
echo.

"%EXE%" --content "%CONTENT%" %*
echo NET_SERVER_EXIT=%ERRORLEVEL%
exit /b %ERRORLEVEL%
