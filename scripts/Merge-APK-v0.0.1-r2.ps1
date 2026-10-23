# Merge the two release parts and verify the original signed APK.
$ErrorActionPreference = 'Stop'
$releaseParts = @(
    @{ Name = 'The-Bazaar-bazaarBot-Mobile-v0.0.1-r2.apk.part001'; Bytes = 1610612736; Hash = '26548e78f1565d23b2bbfd010519202ed30f41a955fad2a264d7bc4b42ebeb84' },
    @{ Name = 'The-Bazaar-bazaarBot-Mobile-v0.0.1-r2.apk.part002'; Bytes = 1378348245; Hash = '3b19561d61b2da717437b3455fa713569120c6fb81268167e25da74ce131b275' }
)
$apkName = 'The Bazaar - bazaarBot Mobile-v0.0.1-r2.apk'
$expectedApkHash = '13dd598acaef6cceec3fd2fc1630c9f4cac19023f4e796f814840c24aa13bfce'
$apkPath = Join-Path $PSScriptRoot $apkName
if (Test-Path -LiteralPath $apkPath) {
    if ((Get-FileHash -LiteralPath $apkPath -Algorithm SHA256).Hash -ieq $expectedApkHash) {
        Write-Host "Already verified: $apkName"
        exit 0
    }
    throw 'An APK with this name already exists and has a different hash. Move it before retrying.'
}
foreach ($releasePart in $releaseParts) {
    $partPath = Join-Path $PSScriptRoot $releasePart.Name
    if (-not (Test-Path -LiteralPath $partPath)) { throw "Missing: $($releasePart.Name)" }
    if ((Get-Item -LiteralPath $partPath).Length -ne $releasePart.Bytes) { throw "Wrong size: $($releasePart.Name)" }
    if ((Get-FileHash -LiteralPath $partPath -Algorithm SHA256).Hash -ine $releasePart.Hash) { throw "Wrong hash: $($releasePart.Name)" }
}
$temporaryApk = "$apkPath.merging"
if (Test-Path -LiteralPath $temporaryApk) { throw 'An unfinished .merging file already exists. Move it before retrying.' }
$apkOutput = [IO.File]::Open($temporaryApk, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
try {
    foreach ($releasePart in $releaseParts) {
        $partInput = [IO.File]::OpenRead((Join-Path $PSScriptRoot $releasePart.Name))
        try { $partInput.CopyTo($apkOutput, 1048576) } finally { $partInput.Dispose() }
    }
} finally { $apkOutput.Dispose() }
if ((Get-FileHash -LiteralPath $temporaryApk -Algorithm SHA256).Hash -ine $expectedApkHash) {
    throw 'Merged APK hash mismatch. The .merging file has been retained for inspection.'
}
Move-Item -LiteralPath $temporaryApk -Destination $apkPath
Write-Host "Verified: $apkName"
Write-Host "SHA256: $expectedApkHash"
Write-Host 'Transfer this APK to your Android device and install it. Keep the official app installed.'
