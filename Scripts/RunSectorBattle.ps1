param([ValidateRange(1,4)][int]$TeamSize=1,[switch]$Packaged,[switch]$NetworkEmulation,[switch]$RenderHost,[int]$ResX=960,[int]$ResY=540)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot/Common.ps1"
if($Packaged){$exe=Join-Path $root 'Artifacts/Windows/PersonaTrial/Binaries/Win64/PersonaTrial.exe';$data=Join-Path $root 'Artifacts/Windows/PersonaTrial'}
else{& "$PSScriptRoot/Build.ps1" -PersonaEngineRoot $PersonaEngineRoot;$exe=Join-Path $PersonaEngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe';$data=$root}
$meta=Join-Path $root '.work/LocalTest/processes.json';$logs=Join-Path $root '.work/BattleTest'
if(Test-Path -LiteralPath $meta){foreach($entry in (Get-Content -LiteralPath $meta -Raw|ConvertFrom-Json)){
    $p=Get-CimInstance Win32_Process -Filter "ProcessId=$($entry.Id)" -ErrorAction SilentlyContinue
    if($p -and $p.ExecutablePath -eq $entry.Executable -and $p.CommandLine.Replace('/','\').Contains(([string]$entry.Project).Replace('/','\'))){throw 'Stop the existing managed test first.'}
}}
New-Item -ItemType Directory -Force -Path $logs|Out-Null
$entries=@()
for($i=0;$i -lt 2*$TeamSize;$i++){
    $team=if($i -lt $TeamSize){'A'}else{'B'};$member=$i%$TeamSize;$gamePort=if($team -eq 'A'){7777}else{7787};$controlPort=if($member -eq 0){$gamePort+100}else{0}
    $flags=@();if(!$Packaged){$flags+=('"'+$root+'/PersonaTrial.uproject"');$flags+='-game'}
    $flags+=@('/Game/Persona/Maps/L_MainMenu','-CustomConfig=Local','-nosteam','-unattended','-windowed',"-ResX=$ResX","-ResY=$ResY",'-ForceRes',"-port=$gamePort","-PersonaBattlePort=$controlPort","-PersonaProfile=Battle$team$member","-PersonaDiagnostics=Player$i",('-abslog="'+$logs+'/Player'+$i+'.log"'))
    if($RenderHost -and $i -eq 0){$flags+='-RenderOffscreen'}else{$flags+=@('-nullrhi','-nosound')}
    if($NetworkEmulation){$flags+=@('-PktLag=100','-PktLoss=2')}
    $inbox=Join-Path $data ".work/Diagnostics/Player$i.command";if(Test-Path -LiteralPath $inbox){Remove-Item -LiteralPath $inbox}
    $p=Start-Process -FilePath $exe -ArgumentList $flags -WorkingDirectory $root -WindowStyle Hidden -PassThru
    $entries+=@{Id=$p.Id;Role=$i;Executable=$exe;Project=if($Packaged){$exe}else{Join-Path $root 'PersonaTrial.uproject'}}
    $entries|ConvertTo-Json|Set-Content -LiteralPath $meta
}
$entries|ForEach-Object{[pscustomobject]$_}|Format-Table
