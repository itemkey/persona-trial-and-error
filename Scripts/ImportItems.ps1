param([string]$PersonaEngineRoot,[string]$Blender='D:\save_artapps\Blender\blender.exe')
. "$PSScriptRoot\Common.ps1"
# Run with the editor closed, after building the current C++ module.
& $Blender --background --disable-autoexec "$PersonaProjectRoot\blender\persona_01.blend" --python "$PSScriptRoot\export_items.py"
if ($LASTEXITCODE -ne 0) { throw 'Item export failed.' }
& (Join-Path $PersonaEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $PersonaProjectFile "-ExecutePythonScript=$PSScriptRoot\import_items.py" -unattended -nosplash -nullrhi -CustomConfig=Local -nosteam
if ($LASTEXITCODE -ne 0) { throw 'Item import failed. Inspect the Unreal Python log.' }
