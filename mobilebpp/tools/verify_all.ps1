# Run every static verification and print a compact summary.
param([string]$Apk = '')
$ErrorActionPreference = "Continue"
. "$PSScriptRoot\..\config.ps1"
$root = $BppRoot
$stage = $BppStage
$py = $BppPy
$failed = 0

Write-Host "== check_lookups"
& $py "$root\mobilebpp\tools\check_lookups.py"
if ($LASTEXITCODE -ne 0) { $failed++ }

Write-Host "== check_offsets"
& $py "$root\mobilebpp\tools\check_offsets.py"
if ($LASTEXITCODE -ne 0) { $failed++ }

Write-Host "== check_hooks"
& $py "$root\mobilebpp\tools\check_hooks.py"
if ($LASTEXITCODE -ne 0) { $failed++ }

Write-Host "== selfcheck"
& $py "$root\mobilebpp\tools\selfcheck.py"
if ($LASTEXITCODE -ne 0) { $failed++ }

Write-Host "== verify_elf libbpp"
& $py "$root\mobilebpp\tools\verify_elf.py" "$root\mobilebpp\build\libbpp.so"
if ($LASTEXITCODE -ne 0) { $failed++ }

$stage = $BppStage
Write-Host "== verify_apk"
if (-not $Apk) {
  $latest = Get-ChildItem "$root\out\TheBazaar-bpp-*.apk" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($latest) { $Apk = $latest.FullName }
}
if (-not $Apk) { Write-Error 'No APK found; pass -Apk explicitly.'; exit 1 }
& $py "$root\mobilebpp\tools\verify_apk.py" "--apk" $Apk
if ($LASTEXITCODE -ne 0) { $failed++ }

Write-Host ""
if ($failed -gt 0) {
  Write-Host "$failed check(s) failed" -ForegroundColor Red
  exit 1
}
Write-Host "all checks passed" -ForegroundColor Green
