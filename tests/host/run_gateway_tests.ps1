$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$workspace=[IO.Path]::GetFullPath((Join-Path $repo '..'))
$outputDir=Join-Path $env:TEMP ('velaguard-gateway-tests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$sources=@('tests\host\gateway_device.c','app\velaguard\src\velaguard_core.c','app\velaguard\src\velaguard_store.c','app\velaguard\src\velaguard_store_file.c','app\velaguard\src\velaguard_runtime.c','app\velaguard\src\velaguard_commands.c') | ForEach-Object {Join-Path $repo $_}
$sources+=Join-Path $workspace 'apps\netutils\cjson\cJSON\cJSON.c'
$includes=@(('-I'+(Join-Path $repo 'app\velaguard\include')),('-I'+(Join-Path $workspace 'apps\netutils\cjson\cJSON')))
$output=Join-Path $outputDir 'gateway_device.exe'
& gcc -std=c11 -Wall -Wextra -Werror @includes @sources -o $output
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
$env:VELAGUARD_GATEWAY_DEVICE=$output
Write-Output ('harness evidence preserved: '+$output)
$env:PYTHONIOENCODING='utf-8'
python -B (Join-Path $repo 'tests\host\test_gateway.py')
exit $LASTEXITCODE
