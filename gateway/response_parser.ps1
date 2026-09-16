function ConvertFrom-VgResponseLine {
  param(
    [AllowEmptyString()][string]$Line,
    [Parameter(Mandatory=$true)][string]$RequestId
  )
  # 只允许行首空白和 SGR；不得从普通日志中搜索或截取 JSON。
  $prefix='\A[ \t\r]*(?:'+[char]27+'\[[0-9;]*m[ \t\r]*)*'
  $json=[regex]::Replace($Line,$prefix,'')
  if(!$json.StartsWith('{')){return $null}
  try{$response=ConvertFrom-Json -InputObject $json -ErrorAction Stop}catch{return $null}
  if($response -isnot [pscustomobject]){return $null}
  if(($response.version -isnot [int] -and $response.version -isnot [long]) -or
     $response.version -ne 1 -or $response.request_id -isnot [string] -or
     $response.request_id -cne $RequestId -or $response.type -isnot [string] -or
     $response.type -cnotin @('response.ok','response.error')){return $null}
  return $response
}
