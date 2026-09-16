$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$workspace=Split-Path $repo
$fixture=Join-Path $repo 'tests\fixtures\taskstore-v2-board-20260915'
$files=@((Join-Path $fixture 'slot0-v2.json'),(Join-Path $fixture 'slot1-v2.json'))
$expected=@('605E290BA05B6C840BE05EDBD9440DDD5081D6C1B6EA1C19609E8D3F4856E0A4','B340662E2E8B3FB14F8D82322C1934CCD42FBA9C6B426C6A9A23CA1D6F820FBC')
for($i=0;$i -lt 2;$i++){if((Get-FileHash $files[$i]).Hash -ne $expected[$i]){throw 'Fixture hash mismatch'}}
$out=Join-Path $env:TEMP ('velaguard-board-migration-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$src=@('tests\host\test_taskstore_board_migration.c','app\velaguard\src\velaguard_core.c','app\velaguard\src\velaguard_store.c') | ForEach-Object {Join-Path $repo $_}
$src+=Join-Path $workspace 'apps\netutils\cjson\cJSON\cJSON.c'
$inc=@(('-I'+(Join-Path $repo 'app\velaguard\include')),('-I'+(Join-Path $workspace 'apps\netutils\cjson\cJSON')))
$exe=Join-Path $out 'migration.exe'
& gcc -std=c11 -Wall -Wextra -Werror @inc @src -o $exe
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $exe @files
$testExit=$LASTEXITCODE
for($i=0;$i -lt 2;$i++){if((Get-FileHash $files[$i]).Hash -ne $expected[$i]){throw 'Fixture changed'}}
Write-Output "Fixture SHA256 unchanged; retained executable: $exe"
exit $testExit