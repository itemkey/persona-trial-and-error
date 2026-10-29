param([string]$PersonaEngineRoot)
. "$PSScriptRoot\Common.ps1"
& (Join-Path $PersonaEngineRoot 'Engine\Build\BatchFiles\Build.bat') -projectfiles "-project=$PersonaProjectFile" -game -engine -progress
if ($LASTEXITCODE -ne 0) { throw "Project generation failed ($LASTEXITCODE). See the UnrealBuildTool error above." }
