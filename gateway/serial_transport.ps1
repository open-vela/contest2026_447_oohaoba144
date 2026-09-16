param(
  [Parameter(Mandatory=$true)][string]$Port,
  [int]$Baud=1000000,
  [switch]$Probe
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'response_parser.ps1')
[Console]::InputEncoding=[Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$serial=[System.IO.Ports.SerialPort]::new($Port,$Baud,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$serial.Handshake=[System.IO.Ports.Handshake]::None
$serial.DtrEnable=$false
$serial.RtsEnable=$false
$serial.Encoding=[System.Text.UTF8Encoding]::new($false)
$serial.ReadTimeout=200
$serial.WriteTimeout=5000
if($Probe){
  # Read-only transport dependency check. Never opens the port.
  [pscustomobject]@{port=$serial.PortName;baud=$serial.BaudRate;dtr=$serial.DtrEnable;rts=$serial.RtsEnable;opened=$serial.IsOpen}|ConvertTo-Json -Compress
  $serial.Dispose()
  exit 0
}
try {
  $serial.Open()
  while($null -ne ($requestLine=[Console]::In.ReadLine())){
    if([string]::IsNullOrWhiteSpace($requestLine)){continue}
    if([Text.Encoding]::UTF8.GetByteCount($requestLine) -gt 1024){throw 'Request exceeds 1024 bytes'}
    $request=$requestLine|ConvertFrom-Json
    if($request.version -ne 1 -or !$request.request_id){throw 'Invalid envelope'}
    $matched=$null
    $pending=[Text.StringBuilder]::new()
    $discard=$false
    for($attempt=0;$attempt -lt 3 -and $null -eq $matched;$attempt++){
      # Reuse the identical envelope for all retries.
      $serial.Write($requestLine+[char]10)
      $watch=[Diagnostics.Stopwatch]::StartNew()
      while($watch.Elapsed.TotalSeconds -lt 5 -and $null -eq $matched){
        $chunk=$serial.ReadExisting()
        foreach($char in $chunk.ToCharArray()){
          if($char -ne [char]10){
            if(!$discard){
              if($pending.Length -ge 2048){[void]$pending.Clear();$discard=$true}
              else{[void]$pending.Append($char)}
            }
            continue
          }
          $line=$pending.ToString()
          [void]$pending.Clear()
          if($discard){$discard=$false;continue}
          $response=ConvertFrom-VgResponseLine -Line $line -RequestId $request.request_id
          if($null -ne $response){
            $matched=$response
            break
          }
        }
        if($null -eq $matched){Start-Sleep -Milliseconds 10}
      }
    }
    if($null -eq $matched){throw 'Response timed out; outcome unknown. Reuse saved envelope or query first.'}
    $matched|ConvertTo-Json -Depth 12 -Compress
    if($matched.type -eq 'response.error'){throw 'Device rejected request; remaining pipeline commands were not sent.'}
  }
} finally {
  if($serial.IsOpen){$serial.Close()}
  $serial.Dispose()
}
