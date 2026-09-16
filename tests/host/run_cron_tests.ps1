$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$workspace=Split-Path $repo
$out=Join-Path $env:TEMP ('velaguard-cron-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$incs=@('app\velaguard\include','tests\host\skill_loader_mock','tests\host\nor_mock') | ForEach-Object { '-I'+(Join-Path $repo $_) }
$incs+=@('packages\ai_agent\include','packages\ai_agent\src','apps\netutils\cjson\cJSON') | ForEach-Object { '-I'+(Join-Path $workspace $_) }
$src=@((Join-Path $repo 'tests\host\test_cron.c'),(Join-Path $workspace 'apps\netutils\cjson\cJSON\cJSON.c'))
$exe=Join-Path $out 'cron_tests.exe'
& gcc -O0 -pthread -std=c11 -Wall -Wextra -Werror -DOK=0 -DERROR=-1 @incs @src -o $exe
if($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "Fixture retained: $out"
foreach($mode in @('normal','failure','rollback','initfault','startfail','range','thread')) {
  & $exe $out $mode
  if($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
exit 0
