$PersonaProjectRoot = Split-Path -Parent $PSScriptRoot
$processFile = Join-Path $PersonaProjectRoot '.work\LocalTest\processes.json'
if (Test-Path -LiteralPath $processFile) {
    foreach ($entry in (Get-Content -Raw -LiteralPath $processFile | ConvertFrom-Json)) {
        $running = Get-CimInstance Win32_Process -Filter "ProcessId=$($entry.Id)" -ErrorAction SilentlyContinue
        if ($running -and $running.ExecutablePath -eq $entry.Executable -and $running.CommandLine.Contains([string]$entry.Project)) {
            Stop-Process -Id $entry.Id
        }
    }
}
