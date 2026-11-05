param([Parameter(Mandatory)][int]$Role,[ValidateSet('Stop','Start','Restart')][string]$Action='Restart')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$metadata=Join-Path $root '.work/LocalTest/processes.json'
$entries=@(Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json)
$entry=$entries | Where-Object { $_.Role -eq $Role } | Select-Object -First 1
if(!$entry){throw 'No managed test role.'}
if($Action -ne 'Start'){
    $running=Get-CimInstance Win32_Process -Filter "ProcessId=$($entry.Id)" -ErrorAction SilentlyContinue
    if(!$running -or $running.ExecutablePath -ne $entry.Executable -or !$running.CommandLine.Replace('/','\').Contains(([string]$entry.Project).Replace('/','\'))){throw 'Managed process identity changed.'}
    $arguments=$running.CommandLine -replace '^("[^"]+"|\S+)\s*',''
    $entry | Add-Member -NotePropertyName Arguments -NotePropertyValue $arguments -Force
    Stop-Process -Id $entry.Id
    Wait-Process -Id $entry.Id -Timeout 15 -ErrorAction SilentlyContinue
    $entries | ConvertTo-Json | Set-Content -LiteralPath $metadata
}
if($Action -ne 'Stop'){
    if(!$entry.Arguments){throw 'No saved launch arguments.'}
    $running=Get-CimInstance Win32_Process -Filter "ProcessId=$($entry.Id)" -ErrorAction SilentlyContinue
    if($running -and $running.ExecutablePath -eq $entry.Executable){throw 'Managed process is still running.'}
    $p=Start-Process -FilePath $entry.Executable -ArgumentList $entry.Arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
    $entry.Id=$p.Id
    $entries | ConvertTo-Json | Set-Content -LiteralPath $metadata
}
