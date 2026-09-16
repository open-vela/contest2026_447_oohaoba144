param([switch]$UnsafeSummaryMutation)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$workspace=Split-Path $repo
$out=Join-Path $env:TEMP ('velaguard-skill-loader-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$inc=@(('-I'+(Join-Path $repo 'app\velaguard\include')),('-I'+(Join-Path $repo 'tests\host\skill_loader_mock')),('-I'+(Join-Path $repo 'tests\host\nor_mock')),('-I'+(Join-Path $workspace 'packages\ai_agent\include')),('-I'+(Join-Path $workspace 'packages\ai_agent\src')))
$module=Join-Path $repo 'app\velaguard\src\velaguard_skill_loader.c'
if($UnsafeSummaryMutation) {
  # 仅在临时测试副本恢复错误返回值；多分配1024字节避免真实堆越界。
  $text=[IO.File]::ReadAllText($module)
  $clamp='return cap ? (int)(cap-1) : 0;'
  $allocation='malloc(VG_SUMMARY_MAX)'
  if(!$text.Contains($clamp) -or !$text.Contains($allocation)) { throw 'Mutation anchor missing' }
  $text=$text.Replace($clamp,'return n;').Replace($allocation,'malloc(VG_SUMMARY_MAX + 1024u)')
  $module=Join-Path $out 'skill_loader_mutation.c'
  [IO.File]::WriteAllText($module,$text,(New-Object Text.UTF8Encoding($false)))
  Write-Output 'TEST MUTATION: unclamped snprintf count; padded allocation; production source unchanged'
}
$src=@((Join-Path $repo 'tests\host\test_skill_loader.c'),$module)
$exe=Join-Path $out 'skill_loader_tests.exe'
& gcc -std=c11 -Wall -Wextra -Werror -DVG_SKILL_HOST_TEST -DOK=0 -DERROR=-1 @inc @src -o $exe
if($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe $out
exit $LASTEXITCODE