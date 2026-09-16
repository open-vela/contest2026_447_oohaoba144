$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).ProviderPath
$workspace=(Resolve-Path (Join-Path $repo '..')).ProviderPath
$includes=@(('-I'+(Join-Path $repo 'app\velaguard\include')),('-I'+(Join-Path $workspace 'apps\netutils\cjson\cJSON')))
$sources=@('tests\host\test_taskstore.c','tests\host\measure_allocations.c','app\velaguard\src\velaguard_core.c','app\velaguard\src\velaguard_store.c') | ForEach-Object {Join-Path $repo $_}
$sources+=Join-Path $workspace 'apps\netutils\cjson\cJSON\cJSON.c'
$out=Join-Path $env:TEMP 'velaguard-host-tests\store_memory.exe'
& gcc -std=c11 -Wall -Wextra -Werror -Dmalloc=vg_measure_malloc -Dcalloc=vg_measure_calloc -Drealloc=vg_measure_realloc -Dfree=vg_measure_free @includes @sources -o $out
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $out
exit $LASTEXITCODE
