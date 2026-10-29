param([switch]$SkipEngine)

$ErrorActionPreference = 'Stop'
$PersonaProjectRoot = Split-Path -Parent $PSScriptRoot
$PersonaProjectFile = Join-Path $PersonaProjectRoot 'PersonaTrial.uproject'

function Get-PersonaEngineCandidates([string]$Association) {
    $registered = Get-ItemProperty -Path 'HKCU:\Software\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
    if ($registered) {
        $match = $registered.PSObject.Properties[$Association]
        if ($match) { $match.Value }
    }
    foreach ($key in @("HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Association", "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$Association")) {
        $install = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if ($install -and $install.InstalledDirectory) { $install.InstalledDirectory }
    }
    # Launcher installations are not always registered under Unreal Engine\Builds.
    $manifest = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path -LiteralPath $manifest) {
        try {
            $installs = (Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).InstallationList
            $installs | Where-Object { $_.AppName -eq "UE_$Association" } | ForEach-Object { $_.InstallLocation }
        } catch { Write-Warning "Cannot read Epic's installation manifest: $manifest" }
    }
    if ($registered -and $Association -match '^\d+\.\d+$') {
        $registered.PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' } | ForEach-Object { $_.Value }
    }
    Join-Path $env:ProgramFiles "Epic Games\UE_$Association"
}

function Resolve-PersonaEngineRoot([string]$RequestedRoot) {
    $association = (Get-Content -LiteralPath $PersonaProjectFile -Raw | ConvertFrom-Json).EngineAssociation
    if (-not $RequestedRoot) { $RequestedRoot = $env:PERSONA_ENGINE_ROOT }
    $candidates = if ($RequestedRoot) { @($RequestedRoot) } else { @(Get-PersonaEngineCandidates $association) }
    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        $versionFile = Join-Path $candidate 'Engine\Build\Build.version'
        if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) { continue }
        try { $version = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json } catch { continue }
        if ($association -match '^\d+\.\d+$' -and "$($version.MajorVersion).$($version.MinorVersion)" -ne $association) { continue }
        if (-not (Test-Path -LiteralPath (Join-Path $candidate 'Engine\Binaries\Win64\UnrealEditor.exe') -PathType Leaf)) { continue }
        return (Resolve-Path -LiteralPath $candidate).Path
    }
    throw "Unreal Engine $association was not found$(if ($RequestedRoot) { " at '$RequestedRoot'" }). Install it with Epic Games Launcher, set PERSONA_ENGINE_ROOT, or pass -PersonaEngineRoot 'path to UE_$association'. See Docs/START_HERE.md."
}

function Assert-PersonaContent {
    $content = Join-Path $PersonaProjectRoot 'Content'
    $required = @('Content/Persona/Maps/L_MainMenu.umap', 'Content/Persona/Maps/L_Hub.umap', 'Content/Persona/Maps/L_Shooting.umap')
    # Include tracked files so a partially checked-out Content directory is caught too.
    if ((Test-Path -LiteralPath (Join-Path $PersonaProjectRoot '.git')) -and (Get-Command git -ErrorAction SilentlyContinue)) {
        $tracked = @(& git -C $PersonaProjectRoot ls-files -- Content)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect tracked game assets with git ls-files.' }
        $required += $tracked
    }
    $missing = @($required | Select-Object -Unique | Where-Object { -not (Test-Path -LiteralPath (Join-Path $PersonaProjectRoot $_) -PathType Leaf) })
    if ($missing.Count) { throw "Game assets are missing: $($missing[0]). Restore the checkout, then run 'git lfs install' and 'git lfs pull' in the project directory." }
    $pointers = @(Get-ChildItem -LiteralPath $content -Recurse -File | Where-Object {
        $_.Length -lt 1024 -and $_.Extension -in @('.uasset', '.umap')
    } | Where-Object { (Get-Content -LiteralPath $_.FullName -TotalCount 1) -eq 'version https://git-lfs.github.com/spec/v1' })
    if ($pointers.Count) { throw "$($pointers.Count) game assets are still Git LFS pointers (including $($pointers[0].Name)). Run 'git lfs install' and 'git lfs pull' in the project directory before launching." }
}

function Get-PersonaPackageExecutable {
    $package = Join-Path $PersonaProjectRoot 'Artifacts\Windows'
    $exe = Join-Path $package 'PersonaTrial\Binaries\Win64\PersonaTrial.exe'
    $paks = Join-Path $package 'PersonaTrial\Content\Paks'
    if ((Test-Path -LiteralPath $exe -PathType Leaf) -and (Test-Path -LiteralPath $paks) -and
        (Get-ChildItem -LiteralPath $paks -Filter '*.pak' -File | Select-Object -First 1)) { return $exe }
}

function Test-PersonaPackageCurrent {
    $receipt = Join-Path $PersonaProjectRoot 'Artifacts\Windows\persona-build.json'
    if (-not (Test-Path -LiteralPath $receipt)) { return $false }
    try {
        $build = Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
        if ($build.Format -ne 1 -or -not $build.SourceHash) { return $false }
    } catch { return $false }
    return $build.SourceHash -eq (Get-PersonaSourceFingerprint)
}

function Get-PersonaSourceFingerprint {
    # Hash names and contents: git can delete files, and copied files can retain old
    # timestamps. Neither case is reliably detected by comparing modification times.
    $inputs = @(Get-Item -LiteralPath $PersonaProjectFile)
    foreach ($directory in @('Source', 'Config', 'Content', 'Scripts')) {
        $inputs += Get-ChildItem -LiteralPath (Join-Path $PersonaProjectRoot $directory) -Recurse -File
    }
    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        $entries = foreach ($file in ($inputs | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($PersonaProjectRoot.Length + 1).Replace('\','/')
            $stream = [IO.File]::OpenRead($file.FullName)
            try { $relative + ':' + [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','') }
            finally { $stream.Dispose() }
        }
        return [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes(($entries -join "`n")))).Replace('-','')
    } finally { $hash.Dispose() }
}

if (-not $SkipEngine) { $PersonaEngineRoot = Resolve-PersonaEngineRoot $PersonaEngineRoot }
