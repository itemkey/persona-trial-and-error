param(
    [string]$PersonaEngineRoot,
    [string]$Blender='D:\save_artapps\Blender\blender.exe',
    [ValidateSet('Preview','Medium','High','Production')][string]$LightingQuality='Preview'
)
. "$PSScriptRoot\Common.ps1"
$editor=Join-Path $PersonaEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$source=Join-Path $PersonaProjectRoot 'blender\persona_01.blend'
foreach($process in (Get-CimInstance Win32_Process -Filter "name='UnrealEditor.exe' OR name='UnrealEditor-Cmd.exe' OR name='PersonaTrial.exe'")) {
    if($process.CommandLine -and $process.CommandLine.Contains($PersonaProjectRoot)) {
        throw 'Close the running PERSONA editor/game before importing or baking its maps.'
    }
}
if(-not(Test-Path -LiteralPath $Blender)){throw "Blender was not found: $Blender"}
$logs=Join-Path $PersonaProjectRoot ".work\Phase4Import\$((Get-Date).ToString('yyyyMMdd-HHmmss'))"
New-Item -ItemType Directory -Path $logs -Force | Out-Null
foreach($name in @('L_Hub.umap','L_Hub_BuiltData.uasset','L_Shooting.umap','L_Shooting_BuiltData.uasset')) {
    $map=Join-Path $PersonaProjectRoot "Content\Persona\Maps\$name"
    if(Test-Path -LiteralPath $map){Copy-Item -LiteralPath $map -Destination (Join-Path $logs $name)}
}
& "$PSScriptRoot\Build.ps1" -PersonaEngineRoot $PersonaEngineRoot
& $Blender --background --disable-autoexec $source --python "$PSScriptRoot\export_phase4.py" *> "$logs\export.log"
if($LASTEXITCODE -ne 0 -or -not(Select-String -LiteralPath "$logs\export.log" -SimpleMatch 'PERSONA_PHASE4_EXPORT_OK' -Quiet)) {throw "Blender export failed. See $logs\export.log"}
& $editor $PersonaProjectFile "-ExecutePythonScript=$PSScriptRoot\import_phase4.py" -unattended -nop4 -nosplash -RenderOffscreen -CustomConfig=Local -nosteam "-abslog=$logs\import.log"
if($LASTEXITCODE -ne 0 -or -not(Select-String -LiteralPath "$logs\import.log" -SimpleMatch 'PERSONA_PHASE4_IMPORT_OK' -Quiet)) {throw "Unreal import failed. See $logs\import.log"}
& $editor $PersonaProjectFile -run=ResavePackages -buildlighting "-quality=$LightingQuality" -mapsonly -projectonly -allowcommandletrendering -map=/Game/Persona/Maps/L_Hub -unattended -nop4 -nosplash -RenderOffscreen -CustomConfig=Local -nosteam "-abslog=$logs\lighting.log"
if($LASTEXITCODE -ne 0){throw "Lighting build failed. See $logs\lighting.log"}
Write-Output "PERSONA import complete. Logs and previous map copies: $logs"
