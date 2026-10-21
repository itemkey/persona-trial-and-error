param([string]$ReportName='gameplay', [string]$DiagnosticsRoot, [string]$PreviewRoot)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$diag=if($DiagnosticsRoot){$DiagnosticsRoot}else{Join-Path $root '.work\Diagnostics'}
if(-not $PreviewRoot){$PreviewRoot=Join-Path $root 'Artifacts\Previews'}
$previewOut=Join-Path $root 'Artifacts\Previews'
New-Item -ItemType Directory -Force -Path $previewOut | Out-Null
$out=Join-Path $root 'Artifacts\Validation'
$checks=[System.Collections.Generic.List[object]]::new()
$roles=@('Player0','Player1','Player2','Player3')
function States {
    $values=@{}
    foreach($role in $roles) {
        $v=Get-Content -LiteralPath "$diag\$role.json" -Raw | ConvertFrom-Json
        if(([DateTime]::UtcNow-([DateTime]$v.utc).ToUniversalTime()).TotalSeconds -gt 10){throw "Stale $role"}
        $values[$role]=$v
    }
    return $values
}
function Send($role,$command) {
    $path="$diag\$role.command"
    $deadline=(Get-Date).AddSeconds(10)
    while(Test-Path -LiteralPath $path) {
        if((Get-Date)-gt $deadline){throw "Unconsumed command: $role"}
        Start-Sleep -Milliseconds 100
    }
    $command|ConvertTo-Json -Compress|Set-Content -LiteralPath "$path.tmp" -Encoding utf8
    Move-Item -LiteralPath "$path.tmp" -Destination $path
}
function Check($name,[scriptblock]$condition,[int]$seconds=15) {
    $deadline=(Get-Date).AddSeconds($seconds)
    do {
        try {$script:s=States; $pass=& $condition} catch {$pass=$false}
        if($pass) {
            $checks.Add(@{check=$name;passed=$true;utc=[DateTime]::UtcNow.ToString('o');host=$s.Player0})
            Write-Output "PASS $name"
            return
        }
        Start-Sleep -Milliseconds 150
    } while((Get-Date)-lt $deadline)
    $checks.Add(@{check=$name;passed=$false;utc=[DateTime]::UtcNow.ToString('o');host=$s.Player0})
    throw "FAIL $name"
}
function Player($id) { $s.Player0.characters|Where-Object {$_.id -eq $id} }
function Z($position) { [double]([regex]::Match($position,'Z=(-?[0-9.]+)').Groups[1].Value) }
function Place($id,$x,$y,$yaw,$pitch=0) {
    Send Player0 @{op='place';player=$id;x=$x;y=$y;z=92.15;yaw=$yaw;pitch=$pitch}
    Start-Sleep -Milliseconds 900
}
function Shot($name) {
    $imagePath=Join-Path $PreviewRoot 'Player0_game.png'
    $previous=if(Test-Path -LiteralPath $imagePath){(Get-Item -LiteralPath $imagePath).LastWriteTimeUtc}else{[DateTime]::MinValue}
    Send Player0 @{op='shot'}
    $deadline=(Get-Date).AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 150
        $updated=(Test-Path -LiteralPath $imagePath) -and (Get-Item -LiteralPath $imagePath).LastWriteTimeUtc -gt $previous
    } until($updated -or (Get-Date)-gt $deadline)
    if(-not $updated){throw 'Screenshot was not saved'}
    Copy-Item -LiteralPath $imagePath -Destination "$previewOut\$name.png"
}
try {
    Check 'Four participants ready for gameplay checks' {
        foreach($role in $roles) {
            $peer=$s[$role]
            if($peer.localPlayer -lt 0 -or @($peer.characters|Where-Object {-not $_.npc -and $_.id -ge 0}).Count -ne 4){return $false}
        }
        return $true
    }
    $hostID=$s.Player0.localPlayer; $clientID=$s.Player1.localPlayer
    Check 'Test participants start alive' {(Player $hostID).health -gt 0 -and (Player $clientID).health -gt 0}
    Send Player0 @{op='npcs';value=0}
    Place $hostID -1600 500 0 -8
    Place $clientID -1100 500 0
    Send Player1 @{op='move';x=1;y=0}
    Check 'Remote walking reaches configured speed' { (Player $clientID).speed -gt 250 }
    Shot 'Persona_walk'
    Send Player1 @{op='sprint';value=$true}
    Check 'Remote sprint consumes replicated stamina' { (Player $clientID).speed -gt 420 -and (Player $clientID).stamina -lt 100 }
    Shot 'Persona_run'
    Send Player1 @{op='move';x=0;y=0}
    Send Player1 @{op='sprint';value=$false}
    Check 'Remote player stops' { (Player $clientID).speed -lt 1 }
    Place $clientID -1200 500 180
    Start-Sleep -Seconds 1
    $s=States; $head=Z (Player $clientID).head; $foot=Z (Player $clientID).footL
    Send Player1 @{op='crouch';value=$true}
    Check 'Crouch lowers body while keeping foot height' {
        $p=Player $clientID
        $p.crouched -and (Z $p.head) -lt $head-30 -and [Math]::Abs((Z $p.footL)-$foot) -lt 2
    }
    Shot 'Persona_crouch'
    Send Player1 @{op='crouch';value=$false}
    Check 'Standing pose restored' { -not (Player $clientID).crouched -and (Z (Player $clientID).head) -gt $head-2 }
    Send Player1 @{op='jump'}
    Check 'Jump replicated with airborne movement' { (Player $clientID).falling -and (Z (Player $clientID).position) -gt 125 }
    Shot 'Persona_jump'
    Check 'Jump lands on floor' { -not (Player $clientID).falling -and (Z (Player $clientID).position) -lt 94 }
    Place $clientID -600 -335 0 -12
    Send Player1 @{op='interact'}
    Check 'Door opens on every peer from client interaction' {
        foreach($r in $roles){if(-not $s[$r].doors[0].open){return $false}}; return $true
    }
    Place $clientID -335 -606 90 -12
    Send Player1 @{op='interact'}
    Check 'Door closes on every peer from client interaction' {
        foreach($r in $roles){if($s[$r].doors[0].open){return $false}}; return $true
    }
    Place $hostID -1200 500 0
    Place $clientID -1600 -500 0
    Send Player0 @{op='npcs';value=1}
    Check 'Behavior Tree patrol runs on server' { @($s.Player0.characters|Where-Object {$_.npc -and $_.brainRunning -and $_.speed -gt 50}).Count -eq 1 }
    $npc=($s.Player0.characters|Where-Object npc).actor
    Send Player0 @{op='place_npc';actor=$npc;x=0;y=500;z=92.15;yaw=0}
    Start-Sleep -Milliseconds 700
    Send Player0 @{op='noise'}
    Check 'AI perception receives a hearing stimulus' {
        if (@($s.Player0.characters|Where-Object {$_.npc -and $_.lastSense -eq 'Hearing'}).Count -eq 1) {return $true}
        # Sight can replace LastSense between half-second snapshots. Repeat the
        # audible stimulus so the observation does not depend on that timing.
        if(-not (Test-Path "$diag\Player0.command")){Send Player0 @{op='noise'}}
        return $false
    } 8
    Send Player0 @{op='npcs';value=0}
    Check 'Test NPC removed' { $s.Player0.npcCount -eq 0 }
    Place $hostID 800 500 180
    $s=States; $health=(Player $hostID).health; $hostActor=(Player $hostID).actor
    Send Player0 @{op='npcs';value=1}
    Check 'New NPC spawned' {$s.Player0.npcCount -eq 1}
    $npc=($s.Player0.characters|Where-Object npc).actor
    Send Player0 @{op='place_npc';actor=$npc;x=0;y=500;z=92.15;yaw=0}
    Check 'AI sees and pursues the player' { @($s.Player0.characters|Where-Object {$_.npc -and $_.target -eq $hostActor -and $_.speed -gt 50}).Count -eq 1 }
    Check 'NPC contact damage reaches player GAS' { (Player $hostID).health -lt $health }
    Shot 'Persona_enemy'
    Send Player0 @{op='npcs';value=0}
} finally {
    foreach($role in @('Player0','Player1')) { if(-not (Test-Path "$diag\$role.command")){Send $role @{op='move';x=0;y=0}} }
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $checks|ConvertTo-Json -Depth 12|Set-Content -LiteralPath "$out\$ReportName.json" -Encoding utf8
}
