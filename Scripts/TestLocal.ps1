param([string]$ReportName='network', [int]$PerfSeconds=0, [int]$ExpectedPacketLag=0, [int]$ExpectedPacketLoss=0, [string]$DiagnosticsRoot, [string]$PreviewRoot)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$diag=if($DiagnosticsRoot){$DiagnosticsRoot}else{Join-Path $projectRoot '.work\Diagnostics'}
if(-not $PreviewRoot){$PreviewRoot=Join-Path $projectRoot 'Artifacts\Previews'}
$reportDir=Join-Path $projectRoot 'Artifacts\Validation'
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$checks=[System.Collections.Generic.List[object]]::new()
$roles=@('Player0','Player1','Player2','Player3')
function Read-States {
    $states=@{}
    foreach($role in $roles) {
        $value=Get-Content -LiteralPath "$diag\$role.json" -Raw | ConvertFrom-Json
        if(([DateTime]::UtcNow-([DateTime]$value.utc).ToUniversalTime()).TotalSeconds -gt 10){throw "Stale snapshot: $role"}
        $states[$role]=$value
    }
    return $states
}
function Send-Command($role,$command) {
    $file="$diag\$role.command"
    $deadline=(Get-Date).AddSeconds(10)
    while(Test-Path -LiteralPath $file) {
        if((Get-Date)-gt $deadline){throw "Unconsumed command: $role"}
        Start-Sleep -Milliseconds 100
    }
    $command | ConvertTo-Json -Compress | Set-Content -LiteralPath "$file.tmp" -Encoding utf8
    Move-Item -LiteralPath "$file.tmp" -Destination $file
}
function Assert-Eventually($name,[scriptblock]$condition,[int]$seconds=20) {
    $deadline=(Get-Date).AddSeconds($seconds)
    do {
        try { $script:s=Read-States; $pass=& $condition } catch { $pass=$false }
        if($pass) {
            $checks.Add(@{check=$name;passed=$true;utc=[DateTime]::UtcNow.ToString('o')})
            Write-Output "PASS $name"
            return
        }
        Start-Sleep -Milliseconds 300
    } while((Get-Date)-lt $deadline)
    $checks.Add(@{check=$name;passed=$false;utc=[DateTime]::UtcNow.ToString('o')})
    throw "FAIL $name"
}
function All-States([scriptblock]$condition) {
    foreach($role in $roles) {if(-not (& $condition $s[$role])) {return $false}}
    return $true
}
function Place-Near($role,$position,[double]$side) {
    $numbers=[regex]::Matches($position,'[XYZ]=(-?[0-9.]+)')
    $x=[double]$numbers[0].Groups[1].Value
    $y=[double]$numbers[1].Groups[1].Value
    $z=[double]$numbers[2].Groups[1].Value
    $yaw=[Math]::Atan2(-$side,185)*180/[Math]::PI
    $pitch=[Math]::Atan2($z-156,[Math]::Sqrt(185*185+$side*$side))*180/[Math]::PI
    Send-Command Player0 @{op='place';player=$s[$role].localPlayer;x=$x-185;y=$y+$side;z=92.15;yaw=$yaw;pitch=$pitch}
    Start-Sleep -Milliseconds 900
}
try {
    Assert-Eventually 'Four connected players and IpNetDriver' {
        All-States {param($v) $v.localPlayer -ge 0 -and @($v.characters | Where-Object {-not $_.npc -and $_.id -ge 0}).Count -eq 4 -and $v.driver -eq 'IpNetDriver'}
    }
    Assert-Eventually 'Requested packet emulation is active on every peer' {
        All-States {param($v) $v.packetLagMs -eq $ExpectedPacketLag -and $v.packetLossPercent -eq $ExpectedPacketLoss}
    }
    $clientID=$s.Player1.localPlayer
    $initialVitals=$s.Player0.characters | Where-Object {$_.id -eq $clientID}
    $damageHealth=[Math]::Max(0,$initialVitals.health-20)
    $foodHunger=[Math]::Min(100,[Math]::Max(0,$initialVitals.hunger-15)+25)
    Send-Command Player1 @{op='damage'}
    Assert-Eventually 'GAS damage replicated to all peers' {
        All-States {param($v) @($v.characters | Where-Object {$_.id -eq $clientID -and $_.health -eq $damageHealth}).Count -eq 1}
    }
    Send-Command Player1 @{op='food'}
    Assert-Eventually 'GAS food replicated to all peers' {
        All-States {param($v) @($v.characters | Where-Object {$_.id -eq $clientID -and $_.hunger -eq $foodHunger}).Count -eq 1}
    }
    $healthBefore=($s.Player0.characters | Where-Object {$_.id -eq $clientID}).health
    Send-Command Player0 @{op='mode';value=1}
    Assert-Eventually 'Two teams of two replicated' {
        All-States {param($v) $v.mode -eq 1 -and @($v.characters | Where-Object {$_.team -eq 0}).Count -eq 2 -and @($v.characters | Where-Object {$_.team -eq 1}).Count -eq 2}
    }
    $teams=@{}; foreach($c in $s.Player0.characters){$teams[[string]$c.id]=$c.team}
    foreach($role in $roles){Send-Command $role @{op='ready'}}
    Assert-Eventually 'Ready flags replicated' {All-States {param($v) @($v.characters | Where-Object {$_.ready}).Count -eq 4}}
    Send-Command Player0 @{op='trial'}
    Assert-Eventually 'Seamless travel to trial retains four participants and health' {
        All-States {param($v) $v.map -eq 'L_TestChamber' -and @($v.characters | Where-Object {-not $_.npc}).Count -eq 4 -and ($v.characters | Where-Object {$_.id -eq $clientID}).health -eq $healthBefore}
    } 40
    Assert-Eventually 'Team assignments survive travel' {
        All-States {param($v) foreach($c in $v.characters | Where-Object {-not $_.npc}) {if($teams[[string]$c.id] -ne $c.team){return $false}};return $true}
    }
    Assert-Eventually 'HUD restored after seamless travel' {All-States {param($v) $v.hudInViewport}}
    Send-Command Player0 @{op='npcs';value=30}
    Assert-Eventually 'Thirty server NPCs replicated' {All-States {param($v) $v.npcCount -eq 30 -and @($v.characters | Where-Object {$_.npc}).Count -eq 30}} 30
    if($PerfSeconds -gt 0) {
        # Observe the active hall from the top of a cover block; NPCs are in view.
        Send-Command Player0 @{op='place';player=$s.Player0.localPlayer;x=1700;y=950;z=272.15;yaw=-90;pitch=-8}
        Start-Sleep -Seconds 3
        Send-Command Player0 @{op='shot'}
        $samples=for($i=0;$i -lt $PerfSeconds*2;$i++) {
            $sample=Get-Content -LiteralPath "$diag\Player0.json" -Raw | ConvertFrom-Json
            $sample | Select-Object utc,map,npcCount,viewportWidth,viewportHeight,screenPercentage,packetLagMs,packetLossPercent,frameMs,gameThreadMs,renderThreadMs,gpuMs,inBytesSec,outBytesSec
            Start-Sleep -Milliseconds 500
        }
        $samples | ConvertTo-Json | Set-Content -LiteralPath "$reportDir\$ReportName-performance.json" -Encoding utf8
        Copy-Item -LiteralPath "$PreviewRoot\Player0_game.png" -Destination "$reportDir\$ReportName-performance-view.png"
    }
    Send-Command Player0 @{op='hub'}
    Assert-Eventually 'Return to hub retains four participants' {All-States {param($v) $v.map -eq 'L_Hub' -and @($v.characters | Where-Object {-not $_.npc}).Count -eq 4}} 40
    Assert-Eventually 'HUD restored in hub on every peer' {All-States {param($v) $v.hudInViewport}}
    $prop=$s.Player0.props[0].actor
    Send-Command Player3 @{op='claim';actor=$prop}
    Start-Sleep -Seconds 2
    Assert-Eventually 'Out-of-range pickup rejected by server' {All-States {param($v) ($v.props | Where-Object {$_.actor -eq $prop}).holder -eq -1}}
    Place-Near Player1 $s.Player0.props[0].position -50
    Place-Near Player2 $s.Player0.props[0].position 50
    Send-Command Player1 @{op='claim';actor=$prop}
    Send-Command Player2 @{op='claim';actor=$prop}
    Assert-Eventually 'Concurrent pickup produces exactly one owner' {
        $holder=($s.Player0.props | Where-Object {$_.actor -eq $prop}).holder
        $holder -ge 0 -and (All-States {param($v) ($v.props | Where-Object {$_.actor -eq $prop}).holder -eq $holder -and @($v.characters | Where-Object {$_.held -eq $prop}).Count -eq 1})
    }
    $holder=($s.Player0.props | Where-Object {$_.actor -eq $prop}).holder
    $winner=@($roles | Where-Object {$s[$_].localPlayer -eq $holder})[0]
    Send-Command $winner @{op='throw'}
    Assert-Eventually 'Throw releases ownership on every peer' {All-States {param($v) ($v.props | Where-Object {$_.actor -eq $prop}).holder -eq -1}}
    Start-Sleep -Seconds 2
    $s=Read-States
    $prop=$s.Player0.props[1].actor
    Place-Near $winner $s.Player0.props[1].position 0
    Send-Command $winner @{op='claim';actor=$prop}
    Assert-Eventually 'Second pickup succeeds after throw' {All-States {param($v) ($v.props | Where-Object {$_.actor -eq $prop}).holder -eq $holder}}
    Send-Command $winner @{op='leave'}
    $roles=@($roles | Where-Object {$_ -ne $winner})
    Assert-Eventually 'Disconnect releases held item and preserves remaining peers' {
        All-States {param($v) @($v.characters | Where-Object {-not $_.npc}).Count -eq 3 -and ($v.props | Where-Object {$_.actor -eq $prop}).holder -eq -1}
    }
    Send-Command Player0 @{op='leave'}
    $roles=@($roles | Where-Object {$_ -ne 'Player0'})
    Assert-Eventually 'Host exit returns clients to hub with an explanation' {
        All-States {param($v) $v.map -eq 'L_Hub' -and $v.netMode -eq 0 -and $v.status -like 'Connection ended*'}
    } 40
} finally {
    @{checks=$checks.ToArray();snapshots=$s;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath "$reportDir\$ReportName.json" -Encoding utf8
}
