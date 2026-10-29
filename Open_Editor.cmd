@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Scripts\Launch.ps1" -Mode Editor %*
set "PersonaExitCode=%ERRORLEVEL%"
if not "%PersonaExitCode%"=="0" pause
exit /b %PersonaExitCode%
