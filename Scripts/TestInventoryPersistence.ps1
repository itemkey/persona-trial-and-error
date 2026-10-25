param([string]$ReportName='phase3-packaged-persistence')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$diag="$root/Artifacts/Windows/PersonaTrial/.work/Diagnostics"
$checks=[System.Collections.Generic.List[object]]::new()
function State($role='Player0'){Get-Content -LiteralPath "$diag/$role.json" -Raw|ConvertFrom-Json}
function Send($role,$command){
    $path="$diag/$role.command";$deadline=(Get-Date).AddSeconds(15)
    while(Test-Path $path){if((Get-Date)-gt $deadline){throw 'Unconsumed command'};Start-Sleep -Milliseconds 100}
    $command|ConvertTo-Json -Compress|Set-Content "$path.tmp" -Encoding utf8;Move-Item "$path.tmp" $path
    Start-Sleep -Milliseconds 700
}
function Check($name,[scriptblock]$predicate,[int]$timeout=40){
    $deadline=(Get-Date).AddSeconds($timeout)
    do {
        try{$script:s=State;$ok=([DateTime]::UtcNow-([DateTime]$s.utc).ToUniversalTime()).TotalSeconds -lt 8 -and (& $predicate)}catch{$ok=$false}
        if($ok){$checks.Add(@{check=$name;passed=$true;state=$s});Write-Output "PASS $name";return};Start-Sleep -Milliseconds 200
    }while((Get-Date)-lt $deadline)
    $checks.Add(@{check=$name;passed=$false;state=$s});throw "FAIL $name"
}
function Slots($c){@($c.slots|ForEach-Object {"$($_.definition):$($_.quantity)"}) -join ','}
function Total($definition){($s.props|Where-Object definition -eq $definition|Measure-Object quantity -Sum).Sum}
try {
    Check 'Packaged host and client ready with saved inventory' {$s.map -like '*L_Hub*' -and @($s.characters|Where-Object {$_.id -ge 0 -and -not $_.npc}).Count -eq 2}
    $client=State Player1;$friend=$s.characters|Where-Object id -eq $client.localPlayer
    $friendId=$friend.persistentId;$friendSlots=Slots $friend;$oldActor=$friend.actor
    Send Player1 @{op='leave'}
    Check 'Disconnect leaves all three inventory slots with the abandoned body' {$c=$s.characters|Where-Object persistentId -eq $friendId;$c.offline -and (Slots $c) -eq $friendSlots}
    Send Player1 @{op='connect'}
    Check 'Rejoin recovers same body and quantities without duplication' {$c=$s.characters|Where-Object persistentId -eq $friendId;-not $c.offline -and $c.actor -eq $oldActor -and (Slots $c) -eq $friendSlots -and (Total DA_Ammo12GA) -eq 44 -and (Total DA_Shotgun) -eq 2}
    Send Player0 @{op='npcs';value=0};Send Player0 @{op='slot';value=0};Send Player0 @{op='save'}
    $s=State;$subject=$s.characters|Where-Object id -eq $s.localPlayer
    $hostId=$subject.persistentId;$hostSlots=Slots $subject;$selected=$subject.selectedSlot;$sector=$s.sector
    $hostProcess=(Get-Content "$root/.work/LocalTest/processes.json" -Raw|ConvertFrom-Json|Where-Object Role -eq 0).Id
    Send Player0 @{op='quit'}
    $deadline=(Get-Date).AddSeconds(45)
    while(Get-CimInstance Win32_Process -Filter "ProcessId=$hostProcess" -ErrorAction SilentlyContinue){if((Get-Date)-gt $deadline){throw 'Host did not exit'};Start-Sleep -Milliseconds 250}
    & "$PSScriptRoot/StopLocal.ps1"
    & "$PSScriptRoot/RunPackagedLocal.ps1" -Players 1 -Menu -Diagnostics -RenderHost
    Check 'Cold process starts in main menu' {$s.map -like '*L_MainMenu*'} 120
    Send Player0 @{op='menu';action='continue';index=0}
    Send Player0 @{op='menu';action='load';index=0}
    Check 'Cold continue restores the selected sector and hotbar' {$c=$s.characters|Where-Object persistentId -eq $hostId;$s.map -like '*L_Hub*' -and $s.sector -eq $sector -and (Slots $c) -eq $hostSlots -and $c.selectedSlot -eq $selected} 90
    Check 'Cold restore preserves world drops and offline inventory exactly once' {(Total DA_Ammo12GA) -eq 44 -and (Total DA_Shotgun) -eq 2 -and (Slots ($s.characters|Where-Object persistentId -eq $friendId)) -eq $friendSlots}
    Check 'Restored equipped item attaches to the new camera' {$c=$s.characters|Where-Object persistentId -eq $hostId;$c.heldAttachedToCamera -and $c.heldViewPositionError -lt .001}
} finally {
    $checks|ConvertTo-Json -Depth 16|Set-Content "$root/Artifacts/Validation/$ReportName.json" -Encoding utf8
}


