param([string]$PersonaEngineRoot)
. "$PSScriptRoot\Common.ps1"
$running = @(Get-CimInstance Win32_Process -Filter "name='UnrealEditor.exe' OR name='UnrealEditor-Cmd.exe' OR name='PersonaTrial.exe' OR name='LiveCodingConsole.exe'" | Where-Object {
    $_.CommandLine -and ($_.CommandLine.Contains($PersonaProjectRoot) -or ($_.Name -eq 'LiveCodingConsole.exe' -and $_.CommandLine.Contains('PersonaTrial')))
})
if ($running.Count) { throw 'Close PERSONA and its Unreal Editor/Live Coding console before packaging, then run Package.ps1 again. Your game is saved on normal exit.' }
Assert-PersonaContent
$startedUtc = [DateTime]::UtcNow.ToString('o')
$sourceHash = Get-PersonaSourceFingerprint
$receipt = Join-Path $PersonaProjectRoot 'Artifacts\Windows\persona-build.json'
# An interrupted package must never be considered current by the launcher.
if (Test-Path -LiteralPath $receipt) { Remove-Item -LiteralPath $receipt }
Push-Location -LiteralPath $PersonaProjectRoot
try {
    & (Join-Path $PersonaEngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun "-project=$PersonaProjectFile" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -prereqs -archive "-archivedirectory=$PersonaProjectRoot\Artifacts\Windows" '-UbtArgs=-MaxParallelActions=4' -utf8output
} finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw "Packaging failed ($LASTEXITCODE)." }
$archive=Join-Path $PersonaProjectRoot 'Artifacts\Windows'
if (-not (Get-PersonaPackageExecutable)) { throw "Packaging did not produce a complete game at $archive." }
@'
@echo off
cd /d "%~dp0"
start "PERSONA Local" "%~dp0PersonaTrial.exe" -CustomConfig=Local -nosteam -windowed -ResX=1920 -ResY=1080 -ForceRes
'@ | Set-Content -LiteralPath (Join-Path $archive 'Start_Local.cmd') -Encoding ascii
@'
@echo off
cd /d "%~dp0"
start "PERSONA Steam" "%~dp0PersonaTrial.exe"
'@ | Set-Content -LiteralPath (Join-Path $archive 'Start_Steam.cmd') -Encoding ascii
foreach ($directory in @($archive,(Join-Path $archive 'PersonaTrial\Binaries\Win64'))) {
    '480' | Set-Content -LiteralPath (Join-Path $directory 'steam_appid.txt') -Encoding ascii
}
if ($sourceHash -eq (Get-PersonaSourceFingerprint)) {
    @{Format=1;StartedUtc=$startedUtc;SourceHash=$sourceHash} | ConvertTo-Json | Set-Content -LiteralPath $receipt -Encoding ascii
} else {
    Write-Warning 'Project files changed during packaging. Repackage before using this build as the current version.'
}
