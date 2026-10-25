param([string]$ReportName='sessions-local', [string]$DiagnosticsRoot)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$diag=if($DiagnosticsRoot){$DiagnosticsRoot}else{Join-Path $root '.work\Diagnostics'}
$roles=@('Player0','Player1','Player2','Player3')
$checks=[System.Collections.Generic.List[object]]::new()
function Send($role,$command) {
    $path=Join-Path $diag "$role.command"
    $deadline=(Get-Date).AddSeconds(10)
    while(Test-Path -LiteralPath $path){
        if((Get-Date)-gt $deadline){throw "Unconsumed command: $role"}
        Start-Sleep -Milliseconds 100
    }
    $command|ConvertTo-Json -Compress|Set-Content -LiteralPath "$path.tmp" -Encoding utf8
    Move-Item -LiteralPath "$path.tmp" -Destination $path
}
function Check($name,[scriptblock]$condition) {
    $deadline=(Get-Date).AddSeconds(35)
    do {
        try {
            $script:s=@{}
            foreach($role in $roles){
                $v=Get-Content -LiteralPath "$diag\$role.json" -Raw|ConvertFrom-Json
                if(([DateTime]::UtcNow-([DateTime]$v.utc).ToUniversalTime()).TotalSeconds -gt 10){throw "Stale $role"}
                $script:s[$role]=$v
            }
            $pass=& $condition
        } catch {$pass=$false}
        if($pass){$checks.Add(@{check=$name;passed=$true});Write-Output "PASS $name";return}
        Start-Sleep -Milliseconds 200
    }while((Get-Date)-lt $deadline)
    $checks.Add(@{check=$name;passed=$false});throw "FAIL $name"
}
try {
    foreach($role in @('Player1','Player2','Player3')){Send $role @{op='leave'}}
    Start-Sleep -Seconds 2
    Send Player0 @{op='leave'}
    Check 'Participants return to the main menu' {
        foreach($role in $roles){if($s[$role].netMode -ne 0 -or $s[$role].map -ne 'L_MainMenu'){return $false}};return $true
    }
    Send Player0 @{op='host'}
    Check 'Null session creation starts a listen server' {$s.Player0.netMode -eq 2 -and $s.Player0.status -like 'Session created*'}
    Send Player1 @{op='find'}
    Check 'LAN search finds a compatible project session' {$s.Player1.status -match '^Found [1-9][0-9]* compatible session'}
    Send Player1 @{op='join'}
    Check 'JoinSession connects the selected participant' {
        $s.Player1.netMode -eq 3 -and $s.Player1.localPlayer -ge 0 -and @($s.Player1.characters|Where-Object {-not $_.npc -and -not $_.offline}).Count -eq 2
    }
    Send Player1 @{op='leave'}
    Check 'DestroySession returns the client to the menu and leaves its body' {$s.Player1.map -eq 'L_MainMenu' -and $s.Player1.netMode -eq 0 -and $s.Player1.status -like 'Session ended*' -and @($s.Player0.characters|Where-Object {-not $_.npc -and -not $_.offline}).Count -eq 1}
    $rehostRequested=[DateTime]::UtcNow
    Send Player0 @{op='host'}
    Check 'Host can replace an existing session' {
        -not (Test-Path -LiteralPath "$diag\Player0.command") -and ([DateTime]$s.Player0.utc).ToUniversalTime() -gt $rehostRequested.AddSeconds(1) -and $s.Player0.netMode -eq 2 -and $s.Player0.status -like 'Session created*'
    }
} finally {
    $out=Join-Path $root 'Artifacts\Validation'
    New-Item -ItemType Directory -Force -Path $out|Out-Null
    @{checks=$checks.ToArray();snapshots=$s;utc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath "$out\$ReportName.json" -Encoding utf8
}
