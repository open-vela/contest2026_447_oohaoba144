$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).ProviderPath
$workspace = (Resolve-Path (Join-Path $repo '..')).ProviderPath
$outputDir = Join-Path $env:TEMP 'velaguard-host-tests'
$output = Join-Path $outputDir 'taskstore_file_tests.exe'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$sources = @(
    (Join-Path $repo 'tests\host\test_taskstore_file.c'),
    (Join-Path $repo 'app\velaguard\src\velaguard_core.c'),
    (Join-Path $repo 'app\velaguard\src\velaguard_store.c'),
    (Join-Path $repo 'app\velaguard\src\velaguard_store_file.c'),
    (Join-Path $workspace 'apps\netutils\cjson\cJSON\cJSON.c')
)
$includes = @(
    ('-I' + (Join-Path $repo 'app\velaguard\include')),
    ('-I' + (Join-Path $workspace 'apps\netutils\cjson\cJSON'))
)
& gcc -std=c11 -Wall -Wextra -Werror @includes @sources -o $output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$testDir = Join-Path $env:TEMP ('velaguard-file-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir | Out-Null
Write-Output "Retained test directory: $testDir"
& $output save $testDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
foreach ($phase in @('load', 'tear', 'recover', 'load')) {
    & $output $phase $testDir
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$inputDir = Join-Path $env:TEMP ('velaguard-file-inputs-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $inputDir | Out-Null
Write-Output "Retained input test directory: $inputDir"
& $output inputs $inputDir
exit $LASTEXITCODE
