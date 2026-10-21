$ErrorActionPreference = 'Stop'
$PersonaProjectRoot = Split-Path -Parent $PSScriptRoot
$PersonaProjectFile = Join-Path $PersonaProjectRoot 'PersonaTrial.uproject'
if (-not $PersonaEngineRoot) {
    $registeredEngines = Get-ItemProperty -Path 'HKCU:\Software\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
    $PersonaEngineRoot = $registeredEngines.'{A5F91D16-4FED-6DA8-F7F5-659730AD4F95}'
    if (-not $PersonaEngineRoot) { $PersonaEngineRoot = 'D:\save_artapps\UE_5.8' }
}
if (-not (Test-Path -LiteralPath (Join-Path $PersonaEngineRoot 'Engine\Build\Build.version'))) { throw 'UE 5.8.3 was not found. Pass -PersonaEngineRoot with the installed engine directory.' }
