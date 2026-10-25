@echo off
if not exist "%~dp0Artifacts\Windows\Start_Local.cmd" (
    echo Windows build not found. Run Scripts\Package.ps1 first.
    pause
    exit /b 1
)
call "%~dp0Artifacts\Windows\Start_Local.cmd"
