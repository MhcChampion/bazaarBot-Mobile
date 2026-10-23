# zipalign + apksigner for the repacked APK.
param(
  [Parameter(Mandatory=$true)][string]$In,
  [Parameter(Mandatory=$true)][string]$Out,
  [string]$Keystore = "",
  [string]$Alias = "bpp",
  [string]$StorePass = "bppbpp",
  [string]$KeyPass = "bppbpp"
)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\..\config.ps1"
if (-not $Keystore) { $Keystore = Join-Path $BppDir "build\bpp.keystore" }
$jdk = $BppJdk
$bt  = $BppBt
$env:JAVA_HOME = $jdk
$env:PATH = "$jdk\bin;$bt;$env:PATH"

if (-not (Test-Path $Keystore)) {
  Write-Host "generating keystore $Keystore"
  & "$jdk\bin\keytool.exe" -genkeypair -v -keystore $Keystore -alias $Alias `
    -keyalg RSA -keysize 2048 -validity 10000 `
    -storepass $StorePass -keypass $KeyPass `
    -dname "CN=BPP Mobile, OU=Mod, O=BPP, L=NA, ST=NA, C=CN" 2>&1 | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "keytool failed" }
}

$aligned = [System.IO.Path]::ChangeExtension($Out, ".aligned.apk")
Write-Host "zipalign -> $aligned"
& "$bt\zipalign.exe" -f -P 16 4 $In $aligned
if ($LASTEXITCODE -ne 0) { throw "zipalign failed" }

Write-Host "apksigner -> $Out"
& "$bt\apksigner.bat" sign --ks $Keystore --ks-key-alias $Alias `
  --ks-pass "pass:$StorePass" --key-pass "pass:$KeyPass" `
  --min-sdk-version 25 --v1-signing-enabled true --v2-signing-enabled true --v3-signing-enabled true `
  --out $Out $aligned
if ($LASTEXITCODE -ne 0) { throw "apksigner sign failed" }

& "$bt\apksigner.bat" verify --print-certs -v $Out
if ($LASTEXITCODE -ne 0) { throw "apksigner verify failed" }
Remove-Item $aligned -Force
Write-Host "signed OK: $Out"
