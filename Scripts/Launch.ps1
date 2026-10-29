param(
    [ValidateSet('Game','Editor')][string]$Mode = 'Game',
    [string]$PersonaEngineRoot,
    [switch]$Packaged
)
. "$PSScriptRoot\Common.ps1" -SkipEngine

if ($Packaged -and $Mode -eq 'Editor') { throw '-Packaged is only valid with -Mode Game.' }
$exe = Get-PersonaPackageExecutable
if ($Mode -eq 'Game' -and $exe -and ($Packaged -or (Test-PersonaPackageCurrent))) {
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -ArgumentList @('-CustomConfig=Local','-nosteam','-windowed','-ResX=1920','-ResY=1080','-ForceRes')
    return
}
if ($Packaged) { throw 'No complete Windows package found. Run Scripts/Package.ps1 or copy the entire Artifacts/Windows folder from the build PC.' }
if ($Mode -eq 'Game') {
    Write-Host 'No current Windows package found. Building and launching the source checkout (the first build can take several minutes).'
}
Assert-PersonaContent
$PersonaEngineRoot = Resolve-PersonaEngineRoot $PersonaEngineRoot
# UBT checks source/header changes and engine compatibility on every launch.
# Merely checking for an existing DLL would run stale code after git pull.
& "$PSScriptRoot\Build.ps1" -PersonaEngineRoot $PersonaEngineRoot
$exe = Join-Path $PersonaEngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$launchArgs = @(('"' + $PersonaProjectFile + '"'))
if ($Mode -eq 'Game') {
    $launchArgs += @('/Game/Persona/Maps/L_MainMenu','-game','-windowed','-ResX=1920','-ResY=1080','-ForceRes')
}
$launchArgs += @('-CustomConfig=Local', '-nosteam')
Start-Process -FilePath $exe -WorkingDirectory $PersonaProjectRoot -ArgumentList $launchArgs
