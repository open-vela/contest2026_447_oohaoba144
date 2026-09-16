[CmdletBinding()]
param(
    [string]$Port = 'COM7',
    [int]$Baud = 1000000,
    [int]$DurationSeconds = 0,
    [string]$OutputRoot = 'D:\date\code\openvela\logs\demo_capture_20260916',
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
if ($Baud -le 0) { throw 'Baud must be positive' }
if ($DurationSeconds -lt 0) { throw 'DurationSeconds must be zero or positive' }

$settings = [ordered]@{
    port = $Port
    baud = $Baud
    data_bits = 8
    parity = 'None'
    stop_bits = 'One'
    handshake = 'None'
    dtr = $false
    rts = $false
    duration_seconds = $DurationSeconds
    opens_serial = -not $ValidateOnly
}
if ($ValidateOnly) {
    $settings | ConvertTo-Json -Depth 4
    exit 0
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $OutputRoot ('run-' + $stamp)
New-Item -ItemType Directory -Path $runDir -Force | Out-Null
$rawPath = Join-Path $runDir 'uart-raw.txt'
$startPath = Join-Path $runDir 'capture-start.json'
$resultPath = Join-Path $runDir 'capture-result.json'
$started = Get-Date
[ordered]@{
    started_at = $started.ToString('o')
    settings = $settings
    raw_log = $rawPath
    note = 'Read-only UART capture; no RPC, reset, flash, or task mutation.'
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $startPath -Encoding utf8

$serial = [IO.Ports.SerialPort]::new(
    $Port, $Baud, [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.Handshake = [IO.Ports.Handshake]::None
$serial.Encoding = [Text.UTF8Encoding]::new($false, $false)
$writer = [IO.StreamWriter]::new($rawPath, $false, [Text.UTF8Encoding]::new($false))
$writer.AutoFlush = $true
$watch = [Diagnostics.Stopwatch]::StartNew()
$completed = $false
try {
    $serial.Open()
    Write-Output ('Capturing {0} at {1} baud. Run directory: {2}' -f $Port, $Baud, $runDir)
    Write-Output 'Use the touchscreen for the demo. This process sends no serial data.'
    while ($DurationSeconds -eq 0 -or $watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $chunk = $serial.ReadExisting()
        if ($chunk.Length) { $writer.Write($chunk) }
        Start-Sleep -Milliseconds 10
    }
    $completed = $true
}
finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
    $writer.Dispose()
    [ordered]@{
        started_at = $started.ToString('o')
        ended_at = (Get-Date).ToString('o')
        elapsed_seconds = [Math]::Round($watch.Elapsed.TotalSeconds, 3)
        completed_duration = $completed
        settings = $settings
        raw_log = $rawPath
        raw_bytes = (Get-Item -LiteralPath $rawPath).Length
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resultPath -Encoding utf8
}

[ordered]@{run_dir=$runDir;raw_log=$rawPath;result=$resultPath} | ConvertTo-Json -Depth 4
