# Local checks that only use tools shipped in this repo (no Unreal).
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

Write-Host "== cpp_core stub sync =="
python scripts/generate_cpp_core_stubs.py --check
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "== cmake configure / build tests =="
cmake -S cpp_core -B cpp_core/build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build cpp_core/build --config Release --target aether_bot_test
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$testExe = Join-Path $RepoRoot "cpp_core\build\Release\aether_bot_test.exe"
if (-not (Test-Path $testExe)) {
    $testExe = Join-Path $RepoRoot "cpp_core\build\aether_bot_test"
}
Write-Host "== aether_bot_test =="
& $testExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "OK"
