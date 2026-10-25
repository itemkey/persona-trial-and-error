$PersonaProjectRoot = Split-Path -Parent $PSScriptRoot
$processFile = Join-Path $PersonaProjectRoot '.work\LocalTest\processes.json'
if (Test-Path -LiteralPath $processFile) {
    foreach ($entry in (Get-Content -Raw -LiteralPath $processFile | ConvertFrom-Json)) {
        $running = Get-CimInstance Win32_Process -Filter "ProcessId=$($entry.Id)" -ErrorAction SilentlyContinue
        if ($running -and $running.ExecutablePath -eq $entry.Executable -and $running.CommandLine.Contains([string]$entry.Project)) {
            # The game can finish a graceful Quit between inspection and this call.
            Stop-Process -Id $entry.Id -ErrorAction SilentlyContinue
            Wait-Process -Id $entry.Id -Timeout 15 -ErrorAction SilentlyContinue
        }
    }
}
