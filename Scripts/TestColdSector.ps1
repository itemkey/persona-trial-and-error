param([string]$ReportName='phase2-packaged-cold-save')
# Run after TestSectors with the managed packaged applications still open.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$diag=Join-Path $root 'Artifacts/Windows/PersonaTrial/.work/Diagnostics'
$checks=[System.Collections.Generic.List[object]]::new()
function State { Get-Content -LiteralPath "$diag/Player0.json" -Raw|ConvertFrom-Json }
function Send($command) {
    $path="$diag/Player0.command"
    $deadline=(Get-Date).AddSeconds(10)
    while(Test-Path -LiteralPath $path){if((Get-Date)-gt $deadline){throw 'Previous command not consumed'};Start-Sleep -Milliseconds 100}
    $command|ConvertTo-Json -Compress|Set-Content -LiteralPath "$path.tmp" -Encoding utf8
    Move-Item -LiteralPath "$path.tmp" -Destination $path
    Start-Sleep -Seconds 1
}
function Check($name,[scriptblock]$predicate) {
    $deadline=(Get-Date).AddSeconds(60)
    do {
        try {$script:s=State; $fresh=([DateTime]::UtcNow-([DateTime]$s.utc).ToUniversalTime()).TotalSeconds -lt 5; $ok=$fresh -and (& $predicate)}catch{$ok=$false}
        if($ok){$checks.Add(@{check=$name;passed=$true});Write-Output "PASS $name";return}
        Start-Sleep -Milliseconds 200
    }while((Get-Date)-lt $deadline)
    $checks.Add(@{check=$name;passed=$false});throw "FAIL $name"
}
try {
    Check 'Packaged host is running an existing saved sector' {$s.netMode -eq 2 -and $s.sector.Length -eq 32}
    Send @{op='place';player=$s.localPlayer;x=-2200;y=-1400;z=92.15;yaw=0;pitch=0}
    Send @{op='crouch';value=$true}
    Send @{op='npcs';value=3}
    Check 'Three NPCs available and host grounded before application shutdown' {$s.npcCount -eq 3 -and -not ($s.characters|Where-Object {$_.id -eq $s.localPlayer}).falling}
    Send @{op='save'}
    $before=State
    $identity=($before.characters|Where-Object {$_.id -eq $before.localPlayer}).persistentId
    $health=($before.characters|Where-Object {$_.id -eq $before.localPlayer}).health
    $npcIds=@($before.characters|Where-Object npc|ForEach-Object persistentId|Sort-Object)
    $sector=$before.sector
    $hostProcess=(Get-Content "$root/.work/LocalTest/processes.json" -Raw|ConvertFrom-Json|Where-Object {$_.Role -eq 0}).Id
    Send @{op='quit'}
    # Quit saves first and asynchronously tears down the renderer. Wait for the
    # actual process to exit before considering this a cold restart.
    $exitDeadline=(Get-Date).AddSeconds(45)
    while(Get-CimInstance Win32_Process -Filter "ProcessId=$hostProcess" -ErrorAction SilentlyContinue) {
        if((Get-Date)-gt $exitDeadline){throw 'The packaged game did not finish graceful Quit'}
        Start-Sleep -Milliseconds 200
    }
    & "$PSScriptRoot/StopLocal.ps1"
    & "$PSScriptRoot/RunPackagedLocal.ps1" -Players 1 -Menu -Diagnostics -RenderHost
    Check 'Fresh process starts in the menu without an active save in memory' {$s.map -eq 'L_MainMenu' -and $s.sector -eq ''}
    Send @{op='menu';action='continue'}
    Send @{op='menu';action='load';index=0}
    Check 'Cold load restores the sector and same host identity, health and crouch' {
        $p=$s.characters|Where-Object {$_.id -eq $s.localPlayer}
        $savedHost=$before.characters|Where-Object {$_.id -eq $before.localPlayer}
        $s.netMode -eq 2 -and $s.sector -eq $sector -and $p.persistentId -eq $identity -and $p.health -eq $health -and $p.crouched -and -not $p.falling -and $p.position -eq $savedHost.position
    }
    Check 'Cold load recreates the same NPC identities with running behavior trees' {
        $restored=@($s.characters|Where-Object npc)
        $s.npcCount -eq 3 -and @($restored|Where-Object brainRunning).Count -eq 3 -and
        (($restored|ForEach-Object persistentId|Sort-Object)-join ',') -eq ($npcIds -join ',')
    }
    Check 'Cold load preserves the door and absent subject' {$s.doors[0].open -and @($s.characters|Where-Object offline).Count -eq 1}
} finally {
    @{checks=$checks.ToArray();before=$before;after=$s;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 12|
        Set-Content -LiteralPath "$root/Artifacts/Validation/$ReportName.json" -Encoding utf8
}
