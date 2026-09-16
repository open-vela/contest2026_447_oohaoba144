$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).ProviderPath
$outputDir = Join-Path $env:TEMP 'velaguard-host-tests'
$output = Join-Path $outputDir 'nor_tests.exe'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$includes = @(
  ('-I' + (Join-Path $repo 'tests\host\nor_mock')),
  ('-I' + (Join-Path $repo 'app\velaguard\include'))
)
$sources = @(
  (Join-Path $repo 'tests\host\test_nor.c'),
  (Join-Path $repo 'app\velaguard\src\velaguard_nor.c')
)
& gcc -std=c11 -Wall -Wextra -Werror @includes @sources -o $output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $output
exit $LASTEXITCODE
