@echo off
setlocal
cd /d "%~dp0"
call "%~dp0run_net_server.bat" --public %*
exit /b %ERRORLEVEL%
