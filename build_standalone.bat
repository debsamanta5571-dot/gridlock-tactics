@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>&1
if errorlevel 1 (
  echo cmake is not on PATH. Install CMake and try again.
  exit /b 1
)

if not exist "cpp_core\CMakeLists.txt" (
  echo Missing cpp_core\CMakeLists.txt. Run this from the repo root.
  exit /b 1
)

if not exist "cpp_core\build\CMakeCache.txt" (
  echo Configuring cpp_core...
  cmake -S cpp_core -B cpp_core\build
  if errorlevel 1 exit /b 1
)

echo Building standalone C++ targets...
cmake --build cpp_core\build --config Release --target tactics_net_server tactics_master_cli tactics_core_cli bot_match
echo STANDALONE_BUILD_EXIT=%ERRORLEVEL%
exit /b %ERRORLEVEL%
