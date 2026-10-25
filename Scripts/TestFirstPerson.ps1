param([string]$ReportName='phase3-packaged-arms')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$diag="$root/Artifacts/Windows/PersonaTrial/.work/Diagnostics"
$shots="$root/Artifacts/Windows/PersonaTrial/Artifacts/Previews"
$out="$root/Artifacts/Previews"
$checks=[System.Collections.Generic.List[object]]::new()
function State {Get-Content "$diag/Player0.json" -Raw|ConvertFrom-Json}
function Send($command){
    $path="$diag/Player0.command";$deadline=(Get-Date).AddSeconds(15)
    while(Test-Path $path){if((Get-Date)-gt $deadline){throw 'Unconsumed command'};Start-Sleep -Milliseconds 80}
    $command|ConvertTo-Json -Compress|Set-Content "$path.tmp" -Encoding utf8;Move-Item "$path.tmp" $path
    Start-Sleep -Milliseconds 650
}
function Player {$s.characters|Where-Object id -eq $s.localPlayer}
function Check($name,[scriptblock]$condition){
    $deadline=(Get-Date).AddSeconds(10)
    do{$script:s=State;$ok=& $condition;if($ok){$checks.Add(@{check=$name;passed=$true;state=$s});Write-Output "PASS $name";return};Start-Sleep -Milliseconds 120}while((Get-Date)-lt $deadline)
    $checks.Add(@{check=$name;passed=$false;state=$s});throw "FAIL $name"
}
function Place {Send @{op='place';player=$s.localPlayer;x=-1900;y=-1200;z=92.15;yaw=0;pitch=0}}
function Shot($name){
    $path="$shots/Player0_game.png";$before=if(Test-Path $path){(Get-Item $path).LastWriteTimeUtc}else{[DateTime]::MinValue}
    Send @{op='shot'};$deadline=(Get-Date).AddSeconds(10)
    do{Start-Sleep -Milliseconds 100;$ready=(Test-Path $path) -and (Get-Item $path).LastWriteTimeUtc -gt $before}while(-not $ready -and (Get-Date)-lt $deadline)
    if(-not $ready){throw 'Screenshot missing'};Copy-Item $path "$out/$name.png"
}
function CopyFresh($before,$name){
    $path="$shots/Player0_game.png";$deadline=(Get-Date).AddSeconds(10)
    do{Start-Sleep -Milliseconds 100;$ready=(Test-Path $path) -and (Get-Item $path).LastWriteTimeUtc -gt $before}while(-not $ready -and (Get-Date)-lt $deadline)
    if(-not $ready){throw 'Jump screenshot missing'};Copy-Item $path "$out/$name.png"
}
function X($str){[double]([regex]::Match($str,'X=(-?[0-9.]+)').Groups[1].Value)}
function Z($str){[double]([regex]::Match($str,'Z=(-?[0-9.]+)').Groups[1].Value)}
try{
    $script:s=State;Send @{op='npcs';value=0};Send @{op='slot';value=2};Send @{op='drop'}
    Place;Shot Phase3_Idle
    Check 'Empty selected slot leaves hands lowered at rest' {(Player).held -eq 'None' -and (Z (Player).handInView) -lt -45}
    Send @{op='move';x=1;y=0}
    Check 'Walking arm pose stays below sight line with positive near-plane clearance' {(Player).speed -gt 340 -and (Z (Player).handInView) -lt -22 -and (X (Player).handInView) -gt 20}
    Shot Phase3_Walk;Send @{op='move';x=0;y=0}
    Place;Send @{op='sprint';value=$true};Send @{op='move';x=1;y=0}
    Check 'Running arms remain below the sight line' {(Player).speed -gt 640 -and (Z (Player).handInView) -lt -22 -and (X (Player).handInView) -gt 20}
    Shot Phase3_Run
    $before=(Get-Item "$shots/Player0_game.png").LastWriteTimeUtc
    Send @{op='jump';shotDelay=.2}
    Check 'Moving jump keeps wrist out of camera and preserves stable arm pose' {(Player).falling -and (X (Player).handInView) -gt 20 -and (Z (Player).handInView) -lt -22}
    CopyFresh $before Phase3_Jump
    Send @{op='move';x=0;y=0};Send @{op='sprint';value=$false}
    Start-Sleep -Seconds 1;Send @{op='place';player=$s.localPlayer;x=-1400;y=0;z=92.15;yaw=0;pitch=0}
    Send @{op='slot';value=0};Send @{op='look';yaw=0;pitch=65};Shot Phase3_Shotgun_LookUp
    Check 'Equipped item remains camera-relative at steep upward pitch' {(Player).heldViewPositionError -lt .001 -and (Player).heldAttachedToCamera}
    Send @{op='look';yaw=0;pitch=-70};Shot Phase3_Shotgun_LookDown
    Check 'Equipped item remains camera-relative at steep downward pitch' {(Player).heldViewPositionError -lt .001 -and (Player).heldViewRotationError -lt .01}
    Send @{op='look';yaw=0;pitch=0};$before=(Get-Item "$shots/Player0_game.png").LastWriteTimeUtc;Send @{op='jump';shotDelay=.2}
    Check 'Equipped jump keeps prop and arms on the same camera frame' {(Player).falling -and (Player).heldViewPositionError -lt .001}
    CopyFresh $before Phase3_Shotgun_Jump
    Start-Sleep -Seconds 1;Send @{op='look';yaw=0;pitch=0};Send @{op='save'}
}finally{$checks|ConvertTo-Json -Depth 14|Set-Content "$root/Artifacts/Validation/$ReportName.json" -Encoding utf8}
