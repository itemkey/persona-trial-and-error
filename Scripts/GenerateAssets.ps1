param([string]$PersonaEngineRoot, [string]$Blender='D:\save_artapps\Blender\blender.exe', [switch]$Reimport)
. "$PSScriptRoot\Common.ps1"
& $Blender --background --disable-autoexec "$PersonaProjectRoot\blender\persona_01.blend" --python "$PSScriptRoot\export_persona.py"
if ($LASTEXITCODE -ne 0) { throw 'Blender export failed.' }
$shared = Join-Path $PersonaEngineRoot 'Templates\TemplateResources\High\Characters\Content'
New-Item -ItemType Directory -Path "$PersonaProjectRoot\Content\Characters" -Force | Out-Null
Copy-Item -Path (Join-Path $shared '*') -Destination "$PersonaProjectRoot\Content\Characters" -Recurse -Force
foreach ($scriptName in @('import_persona.py','build_scene.py','prepare_materials.py','configure_gameplay.py','tune_lighting.py','validate_assets.py')) {
    $extra = if ($Reimport) { '-PersonaReimport' } else { '-PersonaInitialImport' }
    & (Join-Path $PersonaEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $PersonaProjectFile -run=pythonscript "-script=$PSScriptRoot\$scriptName" -unattended -nop4 -nosplash -nullrhi -CustomConfig=Local -nosteam $extra
    if ($LASTEXITCODE -ne 0) { throw "Asset generation failed: $scriptName" }
}
