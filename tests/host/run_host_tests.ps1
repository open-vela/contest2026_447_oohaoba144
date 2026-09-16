$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).ProviderPath
$workspace = (Resolve-Path (Join-Path $repo '..')).ProviderPath
$outputDir = Join-Path $env:TEMP 'velaguard-host-tests'
$output = Join-Path $outputDir 'core_protocol_tests.exe'

New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

$sources = @(
    (Join-Path $repo 'tests\host\test_core_protocol.c'),
    (Join-Path $repo 'app\velaguard\src\velaguard_core.c'),
    (Join-Path $repo 'app\velaguard\src\velaguard_protocol.c'),
    (Join-Path $workspace 'apps\netutils\cjson\cJSON\cJSON.c')
)

$includes = @(
    ('-I' + (Join-Path $repo 'app\velaguard\include')),
    ('-I' + (Join-Path $workspace 'apps\netutils\cjson\cJSON'))
)

& gcc -std=c11 -Wall -Wextra -Werror @includes @sources -o $output
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $output
exit $LASTEXITCODE
