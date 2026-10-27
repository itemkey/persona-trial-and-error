param([string]$PersonaEngineRoot,[ValidateSet('Preview','Medium','High','Production')][string]$Quality='Production')
. "$PSScriptRoot\Common.ps1"
& (Join-Path $PersonaEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $PersonaProjectFile -run=ResavePackages -buildlighting "-quality=$Quality" -mapsonly -projectonly -allowcommandletrendering '-map=/Game/Persona/Maps/L_Hub+/Game/Persona/Maps/L_TestChamber' -unattended -nop4 -nosplash -RenderOffscreen -CustomConfig=Local -nosteam
if ($LASTEXITCODE -ne 0) { throw "Light bake failed ($LASTEXITCODE)." }
