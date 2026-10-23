# One-command release: build libbpp -> inject DT_NEEDED -> repack -> align+sign -> verify.
param(
  [string]$Version = "v0.0.1",
  [string]$PackageName = "com.tempogames.thebazaar.bazaarbotmobile",
  [string]$AppLabel = "The Bazaar - bazaarBot Mobile",
  [int]$VersionCode = 1,
  [switch]$UseLibmain,     # fallback injection target (libmain.so instead of libil2cpp.so)
  [switch]$SkipBuild,
  [switch]$ChecksOnly
)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\..\config.ps1"
$root = $BppRoot
$py = $BppPy
$stage = $BppStage
$apk = Join-Path $root "TheBazaar.apk"
$out = Join-Path $root "out\$AppLabel-$Version.apk"
$unsigned = Join-Path $root "out\_unsigned.apk"

New-Item -ItemType Directory -Force $stage, (Join-Path $root "out") | Out-Null

Write-Host "== 0/5 static checks"
foreach ($chk in @("check_lookups.py", "check_offsets.py", "check_hooks.py", "selfcheck.py")) {
  Write-Host "   $chk"
  & $py (Join-Path $root "mobilebpp\tools\$chk") | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "static check failed: $chk" }
}
if ($ChecksOnly) { Write-Host "static checks passed"; return }

if (-not $SkipBuild) {
  Write-Host "== 1/5 build libbpp.so"
  & pwsh -NoProfile -File (Join-Path $root "mobilebpp\build.ps1")
  if ($LASTEXITCODE -ne 0) { throw "build failed" }
}

Write-Host "== 2/5 inject DT_NEEDED ($(if($UseLibmain){'libmain.so'}else{'libil2cpp.so'}))"
if ($UseLibmain) {
  & $py (Join-Path $root "mobilebpp\tools\extract_entry.py") --apk $apk --entry "lib/arm64-v8a/libmain.so" --out "$stage\libmain.so"
  if ($LASTEXITCODE -ne 0) { throw 'host extraction failed' }
  & $py (Join-Path $root "mobilebpp\tools\inject_needed.py") --elf "$stage\libmain.so" --lib libbpp.so --out "$stage\libmain.bpp.so" --stage "$stage\elf"
  if ($LASTEXITCODE -ne 0) { throw 'host injection failed' }
  $patchArg = @("--libmain-patched", "$stage\libmain.bpp.so")
  $checkElf = "$stage\libmain.bpp.so"
} else {
  & $py (Join-Path $root "mobilebpp\tools\extract_entry.py") --apk $apk --entry "lib/arm64-v8a/libil2cpp.so" --out "$stage\libil2cpp.so"
  if ($LASTEXITCODE -ne 0) { throw 'host extraction failed' }
  & $py (Join-Path $root "mobilebpp\tools\inject_needed.py") --elf "$stage\libil2cpp.so" --lib libbpp.so --out "$stage\libil2cpp.bpp.so" --stage "$stage\elf"
  if ($LASTEXITCODE -ne 0) { throw 'host injection failed' }
  $patchArg = @("--il2cpp-patched", "$stage\libil2cpp.bpp.so")
  $checkElf = "$stage\libil2cpp.bpp.so"
}

Write-Host "== 3/5 verify patched ELF"
& $py (Join-Path $root "mobilebpp\tools\verify_elf.py") $checkElf --expect-needed libbpp.so
if ($LASTEXITCODE -ne 0) { throw "ELF verification failed" }
& $py (Join-Path $root "mobilebpp\tools\verify_elf.py") (Join-Path $root "mobilebpp\build\libbpp.so")
if ($LASTEXITCODE -ne 0) { throw "libbpp ELF verification failed" }

Write-Host "== 4/5 repack + sign"
& $py (Join-Path $root "mobilebpp\tools\patch_apk.py") --apk $apk --lib (Join-Path $root "mobilebpp\build\libbpp.so") @patchArg --out $unsigned --package-name $PackageName --app-label $AppLabel --version-name ($Version -replace '^v', '') --version-code $VersionCode
if ($LASTEXITCODE -ne 0) { throw "repack failed" }
& pwsh -NoProfile -File (Join-Path $root "mobilebpp\tools\sign_apk.ps1") -In $unsigned -Out $out
if ($LASTEXITCODE -ne 0) { throw "sign failed" }

Write-Host "== 5/5 verify APK"
& $py (Join-Path $root "mobilebpp\tools\verify_apk.py") --apk $out --expect-lib libbpp.so --expect-package $PackageName --expect-label $AppLabel --expect-version-name ($Version -replace '^v', '') --expect-version-code $VersionCode
if ($LASTEXITCODE -ne 0) { throw "APK verification failed" }
Remove-Item $unsigned -Force -ErrorAction SilentlyContinue

$hash = (Get-FileHash $out -Algorithm SHA256).Hash
$mb = [math]::Round((Get-Item $out).Length / 1MB, 1)
Write-Host ""
Write-Host "DONE  $out"
Write-Host "      $mb MB  SHA256 $hash"
