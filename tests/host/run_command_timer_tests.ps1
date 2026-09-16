$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$workspace=Split-Path $repo
$outputDir=Join-Path $env:TEMP ('velaguard-command-timers-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$sources=@('tests\host\test_commands_timers.c','app\velaguard\src\velaguard_core.c','app\velaguard\src\velaguard_store.c','app\velaguard\src\velaguard_runtime.c','app\velaguard\src\velaguard_commands.c') | ForEach-Object {Join-Path $repo $_}
$sources+=Join-Path $workspace 'apps\netutils\cjson\cJSON\cJSON.c'
$includes=@(('-I'+(Join-Path $repo 'app\velaguard\include')),('-I'+(Join-Path $workspace 'apps\netutils\cjson\cJSON')))
$output=Join-Path $outputDir 'command_timer_tests.exe'
& gcc -std=c11 -Wall -Wextra -Werror @includes @sources -o $output
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
Write-Output "Fixture retained: $outputDir"
& $output
exit $LASTEXITCODE
