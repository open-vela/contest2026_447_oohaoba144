$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot '..\..\gateway\response_parser.ps1'
. $helper
function Assert-True($condition,[string]$message) {
  if(!$condition){throw $message}
}
$esc=[char]27
$json='{"version":1,"request_id":"fixture-create-a","type":"response.ok","payload":{"task_id":"fixture-create-a","text":"keep \\u001b[0m { intact"}}'
# 真实设备日志所见前缀：cron 日志遗留的 ESC[0m 紧邻完整 JSON 行。
$fixture="$esc[0m$json"
$response=ConvertFrom-VgResponseLine -Line $fixture -RequestId 'fixture-create-a'
Assert-True ($null -ne $response) 'SGR-prefixed real-shape fixture must parse'
Assert-True ($response.payload.text -ceq 'keep \u001b[0m { intact') 'JSON string content changed'
foreach($prefix in @(''," `t", "$esc[0m$esc[32m", " `t$esc[0m `t")) {
  Assert-True ($null -ne (ConvertFrom-VgResponseLine "$prefix$json`r" 'fixture-create-a')) 'valid prefix/CRLF rejected'
}
foreach($prefix in @('log: ', 'junk{', "$esc[2J", "$esc[0K", "x$esc[0m", "$esc[0mcron: ")) {
  Assert-True ($null -eq (ConvertFrom-VgResponseLine "$prefix$json" 'fixture-create-a')) 'unsafe prefix accepted'
}
Assert-True ($null -eq (ConvertFrom-VgResponseLine $fixture 'Fixture-create-a')) 'case-mismatched ID accepted'
foreach($bad in @($json.Replace('"version":1','"version":2'),$json.Replace('"version":1','"version":"1"'),$json.Replace('response.ok','task.list'),$json.Replace('response.ok','RESPONSE.OK'),"[$json]", "$json trailing", '')) {
  Assert-True ($null -eq (ConvertFrom-VgResponseLine $bad 'fixture-create-a')) 'invalid envelope accepted'
}
Assert-True ($null -ne (ConvertFrom-VgResponseLine ($json.Replace('response.ok','response.error')) 'fixture-create-a')) 'response.error rejected'
$escapedJson=$json.Replace('\\u001b','\u001b')
$escaped=ConvertFrom-VgResponseLine $escapedJson 'fixture-create-a'
Assert-True ($escaped.payload.text -ceq "keep $esc[0m { intact") 'escaped SGR inside JSON was altered'
Write-Output 'PASS response parser: SGR fixture, whitespace/CRLF, content preservation, malicious prefixes, strict ID/version/type'
