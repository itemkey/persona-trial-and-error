param([ValidateRange(1,4)][int]$Players=4, [switch]$Visible, [switch]$Diagnostics, [switch]$RenderHost, [switch]$NetworkEmulation, [switch]$Menu, [switch]$CaptureAudio)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $root 'Artifacts\Windows\PersonaTrial\Binaries\Win64\PersonaTrial.exe'
if(-not (Test-Path -LiteralPath $exe)){throw 'Build the Development package with Scripts/Package.ps1 first.'}
$logDir=Join-Path $root '.work\PackagedTest'
$processFile=Join-Path $root '.work\LocalTest\processes.json'
foreach($dir in @($logDir,(Split-Path -Parent $processFile))){New-Item -ItemType Directory -Force -Path $dir|Out-Null}
if(Test-Path -LiteralPath $processFile){
    foreach($entry in (Get-Content -LiteralPath $processFile -Raw|ConvertFrom-Json)){
        $active=Get-CimInstance Win32_Process -Filter "ProcessId=$($entry.Id)" -ErrorAction SilentlyContinue
        if($active -and $active.ExecutablePath -eq $entry.Executable -and $active.CommandLine.Contains([string]$entry.Project)){
            throw 'A managed test is still running. Run Scripts/StopLocal.ps1 first.'
        }
    }
}
$processes=@()
for($i=0;$i -lt $Players;$i++){
    $log=Join-Path $logDir "Player$i.log"
    if(Test-Path -LiteralPath $log){Move-Item -LiteralPath $log -Destination "$log.$((Get-Date).ToString('yyyyMMdd-HHmmss'))"}
    $map=if($Menu){'/Game/Persona/Maps/L_MainMenu'}elseif($i -eq 0){'/Game/Persona/Maps/L_Hub?listen'}else{'127.0.0.1:7777'}
    $launchArgs=@($map,'-CustomConfig=Local','-nosteam','-windowed','-ResX=1920','-ResY=1080','-ForceRes',('-abslog="'+$log+'"'))
    $launchArgs+="-PersonaProfile=Player$i"
    $inbox=Join-Path $root "Artifacts/Windows/PersonaTrial/.work/Diagnostics/Player$i.command"
    if($Diagnostics -and (Test-Path -LiteralPath $inbox)){Remove-Item -LiteralPath $inbox}
    if($Diagnostics){$launchArgs+="-PersonaDiagnostics=Player$i"}
    if($Visible){$style='Normal'}else{
        $style='Hidden'
        if($RenderHost -and $i -eq 0){$launchArgs+='-RenderOffscreen'}else{$launchArgs+=@('-nullrhi','-nosound','-unattended')}
    }
    if($NetworkEmulation){$launchArgs+=@('-PktLag=100','-PktLoss=2')}
    # Per-process override: hidden rendered capture would otherwise be muted by
    # Unreal's default unfocused-volume multiplier. Normal launch is unchanged.
    if($CaptureAudio -and $RenderHost -and $i -eq 0){$launchArgs+='-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0'}
    $p=Start-Process -FilePath $exe -ArgumentList $launchArgs -WindowStyle $style -PassThru
    $processes+=@{Id=$p.Id;Role=$i;Executable=$exe;Project=$exe}
    $processes|ConvertTo-Json|Set-Content -LiteralPath $processFile
    if($i -eq 0 -and -not $Menu){
        $deadline=(Get-Date).AddSeconds(90)
        do{
            Start-Sleep -Milliseconds 300
            $ready=(Test-Path -LiteralPath $log) -and (Select-String -LiteralPath $log -Pattern 'LogNet:.*IpNetDriver.*listening on port' -Quiet)
        }until($ready -or $p.HasExited -or (Get-Date)-gt $deadline)
        if(-not $ready){throw "Packaged host failed to start: $log"}
    }
}
$processes|ForEach-Object{[pscustomobject]$_}|Format-Table
