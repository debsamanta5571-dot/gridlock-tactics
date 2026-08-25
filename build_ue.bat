@echo off
set ENGINE=E:\UE_5.8
set PROJECT=%~dp0TacticsGameUnreal 5.8\TacticsGameUnreal.uproject
set LOG=%~dp0ue_build_log.txt
powershell -NoProfile -Command ^
  "& '%ENGINE%\Engine\Build\BatchFiles\Build.bat' TacticsGameUnrealEditor Win64 Development '-Project=%PROJECT%' -WaitMutex 2>&1 | Tee-Object -FilePath '%LOG%'"
echo UE_BUILD_EXIT=%ERRORLEVEL% >> "%LOG%"
