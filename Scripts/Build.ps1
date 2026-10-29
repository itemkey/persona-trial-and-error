param([string]$PersonaEngineRoot, [ValidateSet('Editor','Game')][string]$Target='Editor')
. "$PSScriptRoot\Common.ps1"
Assert-PersonaContent
$targetName = if ($Target -eq 'Editor') { 'PersonaTrialEditor' } else { 'PersonaTrial' }
& (Join-Path $PersonaEngineRoot 'Engine\Build\BatchFiles\Build.bat') $targetName Win64 Development "-Project=$PersonaProjectFile" -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=4
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE). See the UnrealBuildTool error above. Source builds require Visual Studio C++ Build Tools and a supported Windows SDK; setup instructions are in Docs/START_HERE.md." }
