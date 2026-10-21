@echo off
setlocal
set "PersonaEditor=D:\save_artapps\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
if not exist "%PersonaEditor%" (
    echo Unreal Editor was not found at:
    echo %PersonaEditor%
    pause
    exit /b 1
)
start "" "%PersonaEditor%" "%~dp0PersonaTrial.uproject" -CustomConfig=Local -nosteam
