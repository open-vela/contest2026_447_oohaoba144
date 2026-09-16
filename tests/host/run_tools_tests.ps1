$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$workspace=Split-Path $repo
$out=Join-Path $env:TEMP ('velaguard-tools-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$incs=@('app\velaguard\include','tests\host\skill_loader_mock','tests\host\nor_mock') | ForEach-Object { '-I'+(Join-Path $repo $_) }
$incs+=@('packages\ai_agent\include','packages\ai_agent\src','apps\netutils\cjson\cJSON') | ForEach-Object { '-I'+(Join-Path $workspace $_) }
$src=@('tests\host\test_tools.c','app\velaguard\src\velaguard_tools.c','app\velaguard\src\velaguard_core.c','app\velaguard\src\velaguard_store.c','app\velaguard\src\velaguard_runtime.c','app\velaguard\src\velaguard_commands.c') | ForEach-Object { Join-Path $repo $_ }
$src+=Join-Path $workspace 'apps\netutils\cjson\cJSON\cJSON.c'
# Match the board NOOPT build, with optimization only for the provider-only TU.
$toolSource=Join-Path $repo 'app\velaguard\src\velaguard_tools.c'
$toolObject=Join-Path $out 'velaguard_tools.o'
& gcc -O2 -pthread -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DVG_TOOLS_HOST_TEST -DOK=0 -DERROR=-1 @incs -c $toolSource -o $toolObject
if($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$src=@($src | Where-Object { $_ -ne $toolSource })
$src+=$toolObject
$exe=Join-Path $out 'tools_tests.exe'
& gcc -O0 -pthread -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DVG_TOOLS_HOST_TEST -DOK=0 -DERROR=-1 @incs @src '-Wl,--gc-sections' -o $exe
if($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "Fixture retained: $out"
& $exe $out
exit $LASTEXITCODE
