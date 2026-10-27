param([string]$PersonaEngineRoot,[string]$Blender='D:\save_artapps\Blender\blender.exe',[switch]$Retarget)
. "$PSScriptRoot\Common.ps1"
$source=Join-Path $PersonaProjectRoot 'blender\persona_01.blend'
$before=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
& $Blender --background --disable-autoexec $source --python "$PSScriptRoot\update_hinges.py"
if($LASTEXITCODE -ne 0){throw 'Hinge validation failed.'}
& "$PSScriptRoot\Build.ps1" -PersonaEngineRoot $PersonaEngineRoot
$extra=if($Retarget){@('-PersonaRetargetCombat')}else{@()}
& (Join-Path $PersonaEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $PersonaProjectFile -run=pythonscript "-script=$PSScriptRoot\configure_overhaul.py" -unattended -nop4 -nosplash -nullrhi -CustomConfig=Local -nosteam @extra
if($LASTEXITCODE -ne 0){throw 'Overhaul asset update failed.'}
if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $before){throw 'Source Blender hash changed.'}
