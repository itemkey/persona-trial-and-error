param([string]$DiagnosticsRoot,[string]$ReportName='phase3-editor-inventory',[string]$PreviewRoot)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$diag=if($DiagnosticsRoot){$DiagnosticsRoot}else{"$root/.work/Diagnostics"}
if(-not $PreviewRoot){$PreviewRoot="$root/Artifacts/Previews"}
$checks=[System.Collections.Generic.List[object]]::new()
function States {
    $script:s=@{}
    foreach($role in @('Player0','Player1')){
        $v=Get-Content -LiteralPath "$diag/$role.json" -Raw|ConvertFrom-Json
        if(([DateTime]::UtcNow-([DateTime]$v.utc).ToUniversalTime()).TotalSeconds -gt 8){throw "Stale $role"}
        $script:s[$role]=$v
    }
}
function Send($role,$command){
    $path="$diag/$role.command";$deadline=(Get-Date).AddSeconds(12)
    while(Test-Path -LiteralPath $path){if((Get-Date)-gt $deadline){throw "Unconsumed $role"};Start-Sleep -Milliseconds 100}
    $command|ConvertTo-Json -Compress|Set-Content -LiteralPath "$path.tmp" -Encoding utf8
    Move-Item -LiteralPath "$path.tmp" -Destination $path
}
function Check($name,[scriptblock]$condition,[int]$seconds=20){
    $deadline=(Get-Date).AddSeconds($seconds)
    do {
        try {States;$ok=& $condition}catch{$ok=$false}
        if($ok){$checks.Add(@{check=$name;passed=$true;states=$s});Write-Output "PASS $name";return}
        Start-Sleep -Milliseconds 150
    }while((Get-Date)-lt $deadline)
    $checks.Add(@{check=$name;passed=$false;states=$s});throw "FAIL $name"
}
function Player($role){$s[$role].characters|Where-Object {$_.id -eq $s[$role].localPlayer}}
function Coord($position,$axis){[double]([regex]::Match($position,"$axis=(-?[0-9.]+)").Groups[1].Value)}
function Aim($role,$prop,[double]$side=0){
    States;$id=$s[$role].localPlayer
    $x=Coord $prop.position X;$y=Coord $prop.position Y;$z=Coord $prop.position Z
    $yaw=[Math]::Atan2(-$side,115)*180/[Math]::PI
    $pitch=[Math]::Atan2($z-160.15,[Math]::Sqrt(115*115+$side*$side))*180/[Math]::PI
    Send Player0 @{op='place';player=$id;x=($x-115);y=($y+$side);z=92.15;yaw=$yaw;pitch=$pitch}
    Start-Sleep -Milliseconds 1000
}
function Pick($role,$prop){Aim $role $prop;Send $role @{op='claim';actor=$prop.actor}}
function Count($state,$definition){($state.props|Where-Object definition -eq $definition|Measure-Object -Property quantity -Sum).Sum}
function Shot($name){
    $file="$PreviewRoot/Player0_game.png";$before=if(Test-Path $file){(Get-Item $file).LastWriteTimeUtc}else{[DateTime]::MinValue}
    Send Player0 @{op='shot'};$deadline=(Get-Date).AddSeconds(15)
    do {Start-Sleep -Milliseconds 150;$ready=(Test-Path $file) -and (Get-Item $file).LastWriteTimeUtc -gt $before}while(-not $ready -and (Get-Date)-lt $deadline)
    if($ready){Copy-Item -LiteralPath $file -Destination "$root/Artifacts/Previews/$name.png"}
}
try {
    Check 'Two players have exactly three empty slots' {@((Player Player0).slots).Count -eq 3 -and @((Player Player1).slots).Count -eq 3 -and (Player Player0).held -eq 'None'} 120
    if($ReportName -like '*lag100-loss2*'){Check 'Both peers use 100 ms latency and two percent packet loss' {$s.Player0.packetLagMs -eq 100 -and $s.Player0.packetLossPercent -eq 2 -and $s.Player1.packetLagMs -eq 100 -and $s.Player1.packetLossPercent -eq 2}}
    Send Player0 @{op='npcs';value=0}
    Check 'Blender pickups: two single shotguns and 44 buckshot rounds' {(Count $s.Player0 DA_Shotgun) -eq 2 -and (Count $s.Player0 DA_Ammo12GA) -eq 44}
    $gun=@($s.Player0.props|Where-Object definition -eq DA_Shotgun)[0]
    $ammo=@($s.Player0.props|Where-Object { $_.definition -eq 'DA_Ammo12GA' -and $_.quantity -eq 12 })
    Pick Player0 $gun
    Check 'Shotgun occupies one slot and uses imported mesh' {(Player Player0).slots[0].definition -eq 'DA_Shotgun' -and (Player Player0).slots[0].quantity -eq 1 -and ($s.Player0.props|Where-Object actor -eq $gun.actor).mesh -eq 'SM_Shotgun'}
    Send Player0 @{op='look';yaw=0;pitch=0};Start-Sleep -Seconds 1;Shot 'Phase3_Shotgun'
    Send Player0 @{op='spin';value=240};Start-Sleep -Seconds 4
    Check 'Held visual remains camera attached through fast mouse rotation' {$s.Player0.viewSamples -gt 60 -and $s.Player0.maxViewPositionError -lt .001 -and $s.Player0.maxViewRotationError -lt .01 -and (Player Player0).heldAttachedToCamera}
    Send Player0 @{op='spin';value=0}
    Pick Player0 $ammo[0]
    Check 'Picking ammo while holding shotgun stores it in second slot' {(Player Player0).slots[1].quantity -eq 12 -and (Player Player0).slots[0].definition -eq 'DA_Shotgun'}
    Pick Player0 $ammo[1]
    Check '12 plus 12 splits into 20 and 4 without losing rounds' {(Player Player0).slots[1].quantity -eq 20 -and (Player Player0).slots[2].quantity -eq 4 -and (Count $s.Player0 DA_Ammo12GA) -eq 44}
    $box=$s.Player0.props|Where-Object {$_.definition -eq 'DA_Ammo12GA' -and $_.holder -eq -1}|Select-Object -First 1
    Pick Player0 $box
    Check 'Full hotbar accepts partial stack and leaves four rounds in world' {(Player Player0).slots[2].quantity -eq 20 -and ($s.Player0.props|Where-Object actor -eq $box.actor).quantity -eq 4 -and ($s.Player0.props|Where-Object actor -eq $box.actor).holder -eq -1}
    $otherGun=$s.Player0.props|Where-Object {$_.definition -eq 'DA_Shotgun' -and $_.holder -eq -1}|Select-Object -First 1
    Pick Player0 $otherGun;Start-Sleep -Seconds 1
    Check 'Full hotbar rejects another unstackable shotgun' {($s.Player0.props|Where-Object actor -eq $otherGun.actor).holder -eq -1 -and (Count $s.Player0 DA_Shotgun) -eq 2}
    Send Player0 @{op='slot';value=1};Send Player0 @{op='look';yaw=0;pitch=0}
    Check 'Slot selection equips ammo and stows shotgun on both peers' {(Player Player0).selectedSlot -eq 1 -and ($s.Player0.props|Where-Object actor -eq $gun.actor).stowed -and ($s.Player1.props|Where-Object actor -eq $gun.actor).stowed}
    Shot 'Phase3_Ammo'
    Pick Player1 ($s.Player0.props|Where-Object actor -eq $box.actor)
    Check 'Remote player receives remaining four rounds' {(Player Player1).slots[0].quantity -eq 4 -and (Count $s.Player0 DA_Ammo12GA) -eq 44 -and (Count $s.Player1 DA_Ammo12GA) -eq 44}
    Send Player1 @{op='spin';value=270};Start-Sleep -Seconds 4
    Check 'Remote owner view is stable under camera rotation' {$s.Player1.viewSamples -gt 30 -and $s.Player1.maxViewPositionError -lt .001 -and $s.Player1.maxViewRotationError -lt .01}
    Send Player1 @{op='spin';value=0};Send Player1 @{op='slot';value=3};Start-Sleep -Seconds 1
    Check 'Server rejects out-of-range slot selection' {(Player Player1).selectedSlot -eq 0}
    Send Player0 @{op='slot';value=2};Start-Sleep -Milliseconds 800;Send Player0 @{op='throw'}
    Check 'Throw releases the entire selected stack and frees its slot' {(Player Player0).slots[2].quantity -eq 0 -and @($s.Player0.props|Where-Object {$_.definition -eq 'DA_Ammo12GA' -and $_.holder -eq -1 -and $_.quantity -eq 20}).Count -eq 1}
    Aim Player0 $otherGun -50;Aim Player1 $otherGun 50
    Send Player0 @{op='claim';actor=$otherGun.actor};Send Player1 @{op='claim';actor=$otherGun.actor}
    Check 'Simultaneous pickup awards the shotgun to one owner only' {
        $holders=@($s.Player0.characters|Where-Object {-not $_.npc}|ForEach-Object slots|Where-Object actor -eq $otherGun.actor)
        $holders.Count -eq 1 -and (Count $s.Player0 DA_Shotgun) -eq 2 -and (Count $s.Player1 DA_Shotgun) -eq 2
    }
    Send Player0 @{op='save'};Start-Sleep -Seconds 2;States
    $beforeHost=@((Player Player0).slots|ForEach-Object {"$($_.definition):$($_.quantity)"}) -join ','
    $beforeClient=@((Player Player1).slots|ForEach-Object {"$($_.definition):$($_.quantity)"}) -join ','
    Send Player0 @{op='ready'};Send Player1 @{op='ready'};Start-Sleep -Seconds 1
    Send Player0 @{op='trial'}
    Check 'Hotbars survive group travel to the chamber' {$s.Player0.map -like '*TestChamber*' -and $s.Player1.map -like '*TestChamber*' -and (@((Player Player0).slots|ForEach-Object {"$($_.definition):$($_.quantity)"}) -join ',') -eq $beforeHost -and (@((Player Player1).slots|ForEach-Object {"$($_.definition):$($_.quantity)"}) -join ',') -eq $beforeClient} 90
    Send Player0 @{op='hub'}
    Check 'Return to hub restores drops without respawning consumed pickups' {$s.Player0.map -like '*L_Hub*' -and $s.Player1.map -like '*L_Hub*' -and (Count $s.Player0 DA_Ammo12GA) -eq 44 -and (Count $s.Player0 DA_Shotgun) -eq 2 -and (@((Player Player0).slots|ForEach-Object {"$($_.definition):$($_.quantity)"}) -join ',') -eq $beforeHost} 90
    Start-Sleep -Seconds 3
    Check 'Quick round trip stays alive after the chamber timer deadline' {$s.Player0.map -like '*L_Hub*' -and $s.Player1.map -like '*L_Hub*' -and $s.Player0.npcCount -eq 0}
    Send Player0 @{op='save'}
} finally {
    New-Item -ItemType Directory -Force "$root/Artifacts/Validation"|Out-Null
    $checks|ConvertTo-Json -Depth 16|Set-Content -LiteralPath "$root/Artifacts/Validation/$ReportName.json" -Encoding utf8
}


