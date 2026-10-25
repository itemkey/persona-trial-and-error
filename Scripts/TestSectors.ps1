param([string]$ReportName='phase2-sectors',[string]$DiagnosticsRoot,[string]$PreviewRoot)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$diag=if($DiagnosticsRoot){$DiagnosticsRoot}else{Join-Path $root '.work/Diagnostics'}
if(-not $PreviewRoot){$PreviewRoot=Join-Path $root 'Artifacts/Previews'}
$checks=[System.Collections.Generic.List[object]]::new()
$out=Join-Path $root 'Artifacts/Validation'
New-Item -ItemType Directory -Force -Path $out|Out-Null
function ReadStates {
    $script:s=@{}
    foreach($role in @('Player0','Player1')) {
        $v=Get-Content -LiteralPath "$diag/$role.json" -Raw|ConvertFrom-Json
        if(([DateTime]::UtcNow-([DateTime]$v.utc).ToUniversalTime()).TotalSeconds -gt 5){throw "Stale $role"}
        $script:s[$role]=$v
    }
}
function Send($role,$command) {
    $path="$diag/$role.command"
    $deadline=(Get-Date).AddSeconds(10)
    while(Test-Path -LiteralPath $path) {if((Get-Date)-gt $deadline){throw "Unconsumed $role"};Start-Sleep -Milliseconds 100}
    $command|ConvertTo-Json -Compress|Set-Content -LiteralPath "$path.tmp" -Encoding utf8
    $sent=[DateTime]::UtcNow
    Move-Item -LiteralPath "$path.tmp" -Destination $path
    do {
        Start-Sleep -Milliseconds 100
        $seen=$false
        if(-not(Test-Path -LiteralPath $path)) {
            try { $latest=Get-Content -LiteralPath "$diag/$role.json" -Raw|ConvertFrom-Json; $seen=([DateTime]$latest.utc).ToUniversalTime() -gt $sent } catch {}
        }
        if((Get-Date)-gt $deadline){throw "Command not acknowledged: $role $($command.op)"}
    } until($seen)
}
function Check($name,[scriptblock]$condition,[int]$seconds=25) {
    $deadline=(Get-Date).AddSeconds($seconds)
    do {
        try {ReadStates;$pass=& $condition} catch {$pass=$false}
        if($pass){$checks.Add(@{check=$name;passed=$true});Write-Output "PASS $name";return}
        Start-Sleep -Milliseconds 200
    }while((Get-Date)-lt $deadline)
    $checks.Add(@{check=$name;passed=$false});throw "FAIL $name"
}
function Subject($identity){$s.Player0.characters|Where-Object {$_.persistentId -eq $identity}}
function Place($id,$x,$y,$yaw,$pitch=0){Send Player0 @{op='place';player=$id;x=$x;y=$y;z=92.15;yaw=$yaw;pitch=$pitch};Start-Sleep -Seconds 1}
function Menu($role,$action,$index=0){Send $role @{op='menu';action=$action;index=$index}}
function Shot($name){
    $image="$PreviewRoot/Player0_game.png"
    $before=if(Test-Path -LiteralPath $image){(Get-Item -LiteralPath $image).LastWriteTimeUtc}else{[DateTime]::MinValue}
    Send Player0 @{op='shot'}
    $deadline=(Get-Date).AddSeconds(10)
    do{Start-Sleep -Milliseconds 150}until(((Test-Path -LiteralPath $image)-and (Get-Item -LiteralPath $image).LastWriteTimeUtc -gt $before)-or (Get-Date)-gt $deadline)
    Copy-Item -LiteralPath $image -Destination "$root/Artifacts/Previews/$name.png"
}
try {
    Check 'Both applications start in the main menu without a gameplay pawn' {$s.Player0.map -eq 'L_MainMenu' -and $s.Player1.map -eq 'L_MainMenu' -and $s.Player0.characters.Count -eq 0} 120
    Shot 'Phase2_main_menu'
    Menu Player0 new
    Shot 'Phase2_new_sector'
    Menu Player0 create
    Check 'Create sector starts a new saved listen server' {$s.Player0.netMode -eq 2 -and $s.Player0.sector.Length -eq 32 -and $s.Player0.characters.Count -eq 1}
    $sector=$s.Player0.sector
    $hostID=$s.Player0.localPlayer
    $hostIdentity=($s.Player0.characters|Where-Object {$_.id -eq $hostID}).persistentId
    Menu Player1 find
    Check 'Menu LAN search finds the host' {$s.Player1.sessions -gt 0 -and -not $s.Player1.onlineBusy}
    Menu Player1 join
    Check 'Menu join connects the friend' {$s.Player1.netMode -eq 3 -and $s.Player0.characters.Count -eq 2}
    $friendID=$s.Player1.localPlayer
    $friendIdentity=($s.Player0.characters|Where-Object {$_.id -eq $friendID}).persistentId
    Send Player0 @{op='damage'}
    Send Player1 @{op='damage'}
    Place $friendID -600 -335 0 -12
    Send Player1 @{op='interact'}
    Check 'Door state changes before saving' {$s.Player0.doors[0].open}
    Place $friendID -1380 251 0 -38
    Send Player1 @{op='interact'}
    Check 'Friend holds a server-owned prop' {(Subject $friendIdentity).held -ne 'None'}
    $held=(Subject $friendIdentity).held
    Place $hostID -1540 251 0 -4
    Send Player1 @{op='look';yaw=180;pitch=0}
    Shot 'Phase2_remote_carry'
    Place $hostID -1500 -700 42 -12
    Send Player0 @{op='crouch';value=$true}
    Check 'Host position, crouch and damage established' {(Subject $hostIdentity).crouched -and (Subject $hostIdentity).health -eq 80}
    Send Player1 @{op='leave'}
    Check 'Disconnect preserves the same body, vitals and carried prop' {
        $body=Subject $friendIdentity
        $s.Player1.map -eq 'L_MainMenu' -and $body.offline -and $body.health -eq 80 -and $body.held -eq $held -and $body.speed -lt 1
    }
    Send Player0 @{op='npcs';value=1}
    Check 'NPC available for the absent-body damage check' {$s.Player0.npcCount -eq 1}
    $npc=($s.Player0.characters|Where-Object npc)[0].actor
    # Keep the connected subject out of sight so the fixture tests the abandoned body.
    Place $hostID -2650 -1400 42 -12
    Send Player0 @{op='place_npc';actor=$npc;x=-1250;y=251;z=92.15;yaw=180}
    Check 'NPC damages the disconnected player body' {(Subject $friendIdentity).health -lt 80} 20
    Send Player0 @{op='npcs';value=0}
    Check 'NPC stopped for deterministic save comparison' {$s.Player0.npcCount -eq 0}
    $absentHealth=(Subject $friendIdentity).health
    Place $hostID -1500 -700 42 -12
    Menu Player1 find
    Check 'Friend can search after leaving' {$s.Player1.sessions -gt 0 -and -not $s.Player1.onlineBusy}
    Menu Player1 join
    Check 'Reconnect possesses the existing body without healing or duplication' {
        $body=Subject $friendIdentity
        $s.Player1.netMode -eq 3 -and $s.Player0.characters.Count -eq 2 -and -not $body.offline -and $body.health -eq $absentHealth -and $body.held -eq $held
    }
    $hostBefore=Subject $hostIdentity; $friendBefore=Subject $friendIdentity
    Send Player0 @{op='save'}
    Check 'World save succeeds' {$s.Player0.saveError -eq '' -and $s.Player0.sector -eq $sector}
    Send Player0 @{op='leave'}
    Check 'Host exit returns both applications to the menu' {$s.Player0.map -eq 'L_MainMenu' -and $s.Player1.map -eq 'L_MainMenu'} 40
    Menu Player0 continue
    Shot 'Phase2_sector_archive'
    Menu Player0 load 0
    Check 'Continue restores the same sector and both subjects' {$s.Player0.netMode -eq 2 -and $s.Player0.sector -eq $sector -and $s.Player0.characters.Count -eq 2}
    Check 'Host transform, crouch, health and hunger survive reload' {
        $p=Subject $hostIdentity
        $p.position -eq $hostBefore.position -and $p.crouched -and $p.health -eq $hostBefore.health -and $p.hunger -eq $hostBefore.hunger
    }
    Check 'Friend body, held item and door survive reload' {
        $p=Subject $friendIdentity
        $p.offline -and $p.position -eq $friendBefore.position -and $p.health -eq $friendBefore.health -and $p.held -eq $held -and $s.Player0.doors[0].open
    }
    Send Player1 @{op='connect'}
    Check 'Friend reconnects to a restored sector with their previous state' {$s.Player1.netMode -eq 3 -and -not (Subject $friendIdentity).offline -and (Subject $friendIdentity).health -eq $friendBefore.health -and $s.Player0.characters.Count -eq 2}
    Send Player0 @{op='leave'}
    Check 'Return to menu before creating a second independent sector' {$s.Player0.map -eq 'L_MainMenu' -and $s.Player1.map -eq 'L_MainMenu'} 40
    Menu Player0 create 1
    Check 'New sector has a separate slot and fresh world in team mode' {$s.Player0.netMode -eq 2 -and $s.Player0.sector -ne $sector -and $s.Player0.mode -eq 1 -and $s.Player0.characters.Count -eq 1 -and -not $s.Player0.doors[0].open}
    Send Player0 @{op='leave'}
    Check 'Second sector closes normally' {$s.Player0.map -eq 'L_MainMenu'}
    Menu Player0 continue
    Menu Player0 load 1
    Check 'Original sector remains unchanged after a new sector was created' {$s.Player0.sector -eq $sector -and $s.Player0.netMode -eq 2 -and $s.Player0.doors[0].open -and (Subject $hostIdentity).health -eq 80}
} finally {
    @{checks=$checks.ToArray();snapshots=$s;sector=$sector;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath "$out/$ReportName.json" -Encoding utf8
}
