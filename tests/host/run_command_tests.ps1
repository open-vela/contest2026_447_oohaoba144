$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).ProviderPath
$workspace=(Resolve-Path (Join-Path $repo '..')).ProviderPath
$outputDir=Join-Path $env:TEMP 'velaguard-host-tests'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$sources=@('tests\host\test_commands.c','app\velaguard\src\velaguard_core.c','app\velaguard\src\velaguard_store.c','app\velaguard\src\velaguard_runtime.c','app\velaguard\src\velaguard_commands.c') | ForEach-Object {Join-Path $repo $_}
$sources+=Join-Path $workspace 'apps\netutils\cjson\cJSON\cJSON.c'
$includes=@(('-I'+(Join-Path $repo 'app\velaguard\include')),('-I'+(Join-Path $workspace 'apps\netutils\cjson\cJSON')))
$output=Join-Path $outputDir 'command_tests.exe'
& gcc -std=c11 -Wall -Wextra -Werror @includes @sources -o $output
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $output
exit $LASTEXITCODE
