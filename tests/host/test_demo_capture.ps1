$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$script = Join-Path $repo 'scripts\capture_demo_serial.ps1'
$json = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script `
    -ValidateOnly -Port 'COM99' -Baud 1000000 -DurationSeconds 360
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$result = $json | ConvertFrom-Json
if ($result.port -ne 'COM99') { throw 'Port was not preserved' }
if ($result.baud -ne 1000000) { throw 'Baud was not preserved' }
if ($result.duration_seconds -ne 360) { throw 'Duration was not preserved' }
if ($result.dtr -or $result.rts -or $result.handshake -ne 'None') {
    throw 'Capture must keep DTR/RTS and flow control disabled'
}
if ($result.opens_serial) { throw 'ValidateOnly must not open the serial port' }
Write-Output 'PASS demo capture validation is read-only and preserves serial settings'
