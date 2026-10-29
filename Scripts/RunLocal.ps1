param([string]$PersonaEngineRoot, [ValidateRange(1,4)][int]$Players=4, [switch]$Visible, [switch]$NetworkEmulation, [switch]$Diagnostics, [switch]$Headless, [switch]$RenderHost, [switch]$Menu, [int]$ResX=960, [int]$ResY=540)
. "$PSScriptRoot\Common.ps1"
& "$PSScriptRoot\Build.ps1" -PersonaEngineRoot $PersonaEngineRoot
$editor = Join-Path $PersonaEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logDir = Join-Path $PersonaProjectRoot '.work\LocalTest'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
if (Test-Path -LiteralPath "$logDir\processes.json") {
    foreach ($entry in (Get-Content -LiteralPath "$logDir\processes.json" -Raw | ConvertFrom-Json)) {
        $running=Get-CimInstance Win32_Process -Filter "ProcessId=$($entry.Id)" -ErrorAction SilentlyContinue
        if ($running -and $running.ExecutablePath -eq $entry.Executable -and $running.CommandLine.Contains([string]$entry.Project)) {
            throw 'A managed local test is already running. Run Scripts/StopLocal.ps1 first.'
        }
    }
}
$processes = @()
for ($i=0; $i -lt $Players; $i++) {
    $logFile=Join-Path $logDir "Player$i.log"
    if (Test-Path -LiteralPath $logFile) {
        Move-Item -LiteralPath $logFile -Destination (Join-Path $logDir "Player$i-previous-$((Get-Date).ToString('yyyyMMdd-HHmmss')).log")
    }
    $map = if ($Menu) { '/Game/Persona/Maps/L_MainMenu' } elseif ($i -eq 0) { '/Game/Persona/Maps/L_Hub?listen' } else { '127.0.0.1:7777' }
    if ($Diagnostics -and (Test-Path -LiteralPath "$PersonaProjectRoot\.work\Diagnostics\Player$i.command")) { Remove-Item -LiteralPath "$PersonaProjectRoot\.work\Diagnostics\Player$i.command" }
    $gameArgs = @(('"'+$PersonaProjectFile+'"'),$map,'-game','-CustomConfig=Local','-nosteam','-windowed',"-ResX=$ResX","-ResY=$ResY",'-nosplash',('-abslog="'+$logDir+'\Player'+$i+'.log"'))
    $gameArgs += "-PersonaProfile=Player$i"
    if ($Diagnostics) { $gameArgs += "-PersonaDiagnostics=Player$i" }
    if ($Headless -and -not ($RenderHost -and $i -eq 0)) { $gameArgs += '-nullrhi'; $gameArgs += '-nosound'; $gameArgs += '-unattended' }
    elseif (-not $Visible) { $gameArgs += '-RenderOffscreen'; $gameArgs += '-ForceRes' }
    if ($NetworkEmulation) { $gameArgs += '-PktLag=100'; $gameArgs += '-PktLoss=2' }
    # Hidden rendered tests must not wait forever on an invisible startup dialog.
    if (-not $Visible -and $gameArgs -notcontains '-unattended') { $gameArgs += '-unattended' }
    $windowStyle = if ($Visible) { 'Normal' } else { 'Hidden' }
    $process = Start-Process -FilePath $editor -ArgumentList $gameArgs -WindowStyle $windowStyle -PassThru
    $processes += @{Id=$process.Id;Role=$i;Executable=$editor;Project=$PersonaProjectFile}
    $processes | ConvertTo-Json | Set-Content -LiteralPath "$logDir\processes.json"
    if ($i -eq 0 -and -not $Menu) {
        $deadline=(Get-Date).AddSeconds(300)
        do {
            Start-Sleep -Milliseconds 500
            $ready = (Test-Path "$logDir\Player0.log") -and ((Get-Item -LiteralPath "$logDir\Player0.log").LastWriteTime -ge $process.StartTime) -and (Select-String -LiteralPath "$logDir\Player0.log" -Pattern 'LogNet:.*IpNetDriver.*listening on port' -Quiet)
        } until ($ready -or (Get-Date) -gt $deadline -or $process.HasExited)
        if (-not $ready) { throw 'Host did not become ready. Inspect .work/LocalTest/Player0.log.' }
    }
}
$processes | ConvertTo-Json | Set-Content -LiteralPath "$logDir\processes.json"
$processes | Format-Table
