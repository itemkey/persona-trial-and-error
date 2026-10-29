# Launcher regression tests. Uses a fake engine/package and intercepts process launch;
# no game, compiler, downloads, or user save files are involved. Works in PowerShell 5.1.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $root ('.work\LaunchTests\' + [Guid]::NewGuid().ToString('N') + '\Project with spaces')
$originalEngineEnv = $env:PERSONA_ENGINE_ROOT
$originalBuildExit = $env:PERSONA_TEST_BUILD_EXIT
$launches = New-Object 'System.Collections.Generic.List[object]'
$checks = 0
function Assert($Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    $script:checks++
    Write-Host "PASS: $Message"
}
function ExpectFailure([scriptblock]$Action, [string]$Pattern) {
    $message = ''
    try { & $Action | Out-Null } catch { $message = $_.Exception.Message }
    Assert ($message -like $Pattern) "Failure explains $Pattern"
}
function Start-Process {
    param($FilePath, $WorkingDirectory, $ArgumentList)
    $launches.Add(@{FilePath=$FilePath;WorkingDirectory=$WorkingDirectory;Arguments=$ArgumentList})
}
function Write-Receipt {
    @{Format=1;SourceHash=(Get-PersonaSourceFingerprint)} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $fixture 'Artifacts\Windows\persona-build.json') -Encoding ascii
}
try {
    $engine = Join-Path $fixture 'Engine install with spaces'
    foreach ($directory in @('Scripts','Content\Persona\Maps','Config','Source','Engine install with spaces\Engine\Build\BatchFiles','Engine install with spaces\Engine\Binaries\Win64')) {
        New-Item -ItemType Directory -Path (Join-Path $fixture $directory) -Force | Out-Null
    }
    foreach ($name in @('Common.ps1','Build.ps1','Launch.ps1')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $fixture "Scripts\$name")
    }
    '{"EngineAssociation":"5.8"}' | Set-Content -LiteralPath (Join-Path $fixture 'PersonaTrial.uproject') -Encoding ascii
    '{"MajorVersion":5,"MinorVersion":8,"PatchVersion":3}' | Set-Content -LiteralPath (Join-Path $engine 'Engine\Build\Build.version') -Encoding ascii
    'test executable' | Set-Content -LiteralPath (Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe') -Encoding ascii
    @'
@echo off
echo PERSONA_TEST_BUILD_CALLED %*
exit /b %PERSONA_TEST_BUILD_EXIT%
'@ | Set-Content -LiteralPath (Join-Path $engine 'Engine\Build\BatchFiles\Build.bat') -Encoding ascii
    foreach ($map in @('L_MainMenu','L_Hub','L_Shooting')) {
        ('X' * 2048) | Set-Content -LiteralPath (Join-Path $fixture "Content\Persona\Maps\$map.umap") -Encoding ascii
    }
    . (Join-Path $fixture 'Scripts\Common.ps1') -SkipEngine
    # Include punctuation-sensitive names to catch cross-runtime collation changes.
    foreach ($name in @('A.cpp','A_B.cpp','AB.cpp')) {
        'test source' | Set-Content -LiteralPath (Join-Path $fixture "Source\$name") -Encoding ascii
    }
    $fingerprintProbe = Join-Path $fixture 'ReadFingerprint.ps1'
    '. "$PSScriptRoot\Scripts\Common.ps1" -SkipEngine; Get-PersonaSourceFingerprint' |
        Set-Content -LiteralPath $fingerprintProbe -Encoding ascii
    $expectedFingerprint = Get-PersonaSourceFingerprint
    foreach ($hostName in @('powershell.exe','pwsh.exe')) {
        $hostCommand = Get-Command $hostName -ErrorAction SilentlyContinue
        if ($hostCommand) {
            $actualFingerprint = & $hostCommand.Source -NoProfile -ExecutionPolicy Bypass -File $fingerprintProbe
            Assert ($LASTEXITCODE -eq 0 -and $actualFingerprint -eq $expectedFingerprint) "Fingerprint agrees with $hostName"
        }
    }
    $env:PERSONA_ENGINE_ROOT = $engine
    $env:PERSONA_TEST_BUILD_EXIT = '0'
    Assert ((Resolve-PersonaEngineRoot '') -eq $engine) 'Engine environment override supports paths with spaces'
    ExpectFailure { Resolve-PersonaEngineRoot (Join-Path $fixture 'missing engine') } '*Unreal Engine 5.8 was not found*'
    '{"MajorVersion":5,"MinorVersion":7}' | Set-Content -LiteralPath (Join-Path $engine 'Engine\Build\Build.version') -Encoding ascii
    ExpectFailure { Resolve-PersonaEngineRoot $engine } '*Unreal Engine 5.8 was not found*'
    '{"MajorVersion":5,"MinorVersion":8,"PatchVersion":3}' | Set-Content -LiteralPath (Join-Path $engine 'Engine\Build\Build.version') -Encoding ascii
    Assert-PersonaContent
    $pointer = Join-Path $fixture 'Content\Persona\Missing.uasset'
    'version https://git-lfs.github.com/spec/v1' | Set-Content -LiteralPath $pointer -Encoding ascii
    ExpectFailure { & (Join-Path $fixture 'Scripts\Launch.ps1') } '*Git LFS pointers*'
    Assert ($launches.Count -eq 0) 'Unresolved assets never launch the game'
    Remove-Item -LiteralPath $pointer
    $map = Join-Path $fixture 'Content\Persona\Maps\L_MainMenu.umap'
    Remove-Item -LiteralPath $map
    ExpectFailure { Assert-PersonaContent } '*Game assets are missing*'
    ('X' * 2048) | Set-Content -LiteralPath $map -Encoding ascii

    $output = @(& (Join-Path $fixture 'Scripts\Launch.ps1')) -join "`n"
    Assert ($output -like '*PERSONA_TEST_BUILD_CALLED PersonaTrialEditor*') 'Fresh checkout builds the editor module before game launch'
    Assert ($launches.Count -eq 1 -and $launches[0].FilePath -eq (Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor.exe')) 'Fresh checkout launches the detected engine'
    Assert ($launches[0].Arguments -contains '-game' -and $launches[0].Arguments -contains '/Game/Persona/Maps/L_MainMenu') 'Source game opens the main menu in game mode'
    Assert ($launches[0].Arguments -contains '-CustomConfig=Local' -and $launches[0].Arguments -contains '-nosteam') 'Local launch does not require Steam'
    Assert ($launches[0].Arguments[0] -eq ('"' + (Join-Path $fixture 'PersonaTrial.uproject') + '"')) 'Project argument is quoted for paths with spaces'
    $launches.Clear()
    $output = @(& (Join-Path $fixture 'Scripts\Launch.ps1') -Mode Editor) -join "`n"
    Assert ($output -like '*PERSONA_TEST_BUILD_CALLED*' -and $launches.Count -eq 1 -and $launches[0].Arguments -notcontains '-game') 'Editor launch also builds and uses editor mode'
    $launches.Clear()
    $env:PERSONA_TEST_BUILD_EXIT = '6'
    ExpectFailure { & (Join-Path $fixture 'Scripts\Launch.ps1') } '*Build failed (6)*'
    Assert ($launches.Count -eq 0) 'Failed build never launches an old module'
    $env:PERSONA_TEST_BUILD_EXIT = '0'

    $package = Join-Path $fixture 'Artifacts\Windows'
    New-Item -ItemType Directory -Path (Join-Path $package 'PersonaTrial\Binaries\Win64') -Force | Out-Null
    $packageExe = Join-Path $package 'PersonaTrial\Binaries\Win64\PersonaTrial.exe'
    'test executable' | Set-Content -LiteralPath $packageExe -Encoding ascii
    ExpectFailure { & (Join-Path $fixture 'Scripts\Launch.ps1') -Packaged } '*No complete Windows package*'
    New-Item -ItemType Directory -Path (Join-Path $package 'PersonaTrial\Content\Paks') -Force | Out-Null
    ExpectFailure { & (Join-Path $fixture 'Scripts\Launch.ps1') -Packaged } '*No complete Windows package*'
    'test cooked content' | Set-Content -LiteralPath (Join-Path $package 'PersonaTrial\Content\Paks\PersonaTrial-Windows.pak') -Encoding ascii
    Write-Receipt
    $env:PERSONA_ENGINE_ROOT = Join-Path $fixture 'no engine installed'
    & (Join-Path $fixture 'Scripts\Launch.ps1')
    Assert ($launches.Count -eq 1 -and $launches[0].FilePath -eq $packageExe) 'Current package launches without an engine or compiler'
    $launches.Clear()
    $oldTime = (Get-Item -LiteralPath $map).LastWriteTimeUtc
    ('Y' * 2048) | Set-Content -LiteralPath $map -Encoding ascii
    (Get-Item -LiteralPath $map).LastWriteTimeUtc = $oldTime
    Assert (-not (Test-PersonaPackageCurrent)) 'Content changes make the package stale even with unchanged size and timestamp'
    & (Join-Path $fixture 'Scripts\Launch.ps1') -Packaged
    Assert ($launches.Count -eq 1 -and $launches[0].FilePath -eq $packageExe) 'Explicit packaged mode supports copied or older builds without an engine'
    $launches.Clear()
    $env:PERSONA_ENGINE_ROOT = $engine
    $output = @(& (Join-Path $fixture 'Scripts\Launch.ps1')) -join "`n"
    Assert ($output -like '*PERSONA_TEST_BUILD_CALLED*' -and $launches[0].FilePath -ne $packageExe) 'Default launch rebuilds source instead of playing a stale package'
    $launches.Clear()
    $extraSource = Join-Path $fixture 'Source\Removed.cpp'
    'test source' | Set-Content -LiteralPath $extraSource -Encoding ascii
    Write-Receipt
    Remove-Item -LiteralPath $extraSource
    Assert (-not (Test-PersonaPackageCurrent)) 'Deleted source files also invalidate the package'
    'invalid json' | Set-Content -LiteralPath (Join-Path $package 'persona-build.json') -Encoding ascii
    Assert (-not (Test-PersonaPackageCurrent)) 'Invalid package receipt cannot bypass rebuilding'
    Remove-Item -LiteralPath (Join-Path $package 'persona-build.json')
    Assert (-not (Test-PersonaPackageCurrent)) 'Legacy packages without a receipt are not assumed current'
    ExpectFailure { & (Join-Path $fixture 'Scripts\Launch.ps1') -Mode Editor -Packaged } '*only valid with -Mode Game*'
    Write-Host "All $checks launcher checks passed."
} finally {
    $env:PERSONA_ENGINE_ROOT = $originalEngineEnv
    $env:PERSONA_TEST_BUILD_EXIT = $originalBuildExit
    # Verify the exact generated target before recursively removing this test fixture.
    $allowed = [IO.Path]::GetFullPath((Join-Path $root '.work\LaunchTests')) + [IO.Path]::DirectorySeparatorChar
    $resolved = [IO.Path]::GetFullPath($fixture)
    if ($resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolved)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
