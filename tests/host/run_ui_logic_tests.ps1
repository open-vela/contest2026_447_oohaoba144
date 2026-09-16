$ErrorActionPreference = 'Stop'

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$workspace = Split-Path $repo -Parent
$outputDir = Join-Path $env:TEMP ('velaguard-ui-logic-' + [guid]::NewGuid().ToString('N'))
$output = Join-Path $outputDir 'ui_logic_tests.exe'

New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

$sources = @(
    (Join-Path $repo 'tests\host\test_ui_logic.c'),
    (Join-Path $repo 'app\velaguard\src\velaguard_ui_logic.c'),
    (Join-Path $repo 'app\velaguard\src\velaguard_core.c'),
    (Join-Path $repo 'app\velaguard\src\velaguard_store.c'),
    (Join-Path $repo 'app\velaguard\src\velaguard_runtime.c'),
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

Write-Output ("Retained executable: " + $output)
& $output
exit $LASTEXITCODE
