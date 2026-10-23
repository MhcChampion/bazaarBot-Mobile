# Collect a BPP Mobile test report from a connected device.
#
# Pulls the mod log, the data dump and a filtered logcat, adds device/package
# info, and zips everything so it can be sent in one file.
param(
  [string]$Package = "com.tempogames.thebazaar",
  [string]$Serial = "",
  [string]$Output  = "",
  [int]$WaitSeconds = 3,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\..\config.ps1"
$root = $BppRoot
if (-not $Output) {
  $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
  $Output = Join-Path $root "out\bpp-report-$stamp.zip"
}
$work = Join-Path $root "out\_report"
$filesDir = "/sdcard/Android/media/$Package"
$legacyDir = "/sdcard/Android/data/$Package/files"

function Need-Adb {
  if ($env:BPP_ADB -and (Test-Path -LiteralPath $env:BPP_ADB)) { return $env:BPP_ADB }
  $adb = Get-Command adb -ErrorAction SilentlyContinue
  if (-not $adb) {
    $mumu = Join-Path ([IO.Path]::GetPathRoot($root)) 'mumu\MuMuPlayer\nx_device\12.0\shell\adb.exe'
    if (Test-Path -LiteralPath $mumu) { return $mumu }
  }
  if (-not $adb) {
    Write-Host "未找到 adb。请安装 Android platform-tools 并加入 PATH，或把 adb.exe 所在目录加进 PATH。" -ForegroundColor Yellow
    Write-Host "下载：https://developer.android.com/tools/releases/platform-tools" -ForegroundColor Yellow
    return $null
  }
  return $adb.Source
}

if ($DryRun) {
  Write-Host "[dry-run] would collect:"
  Write-Host "  device props, package list, $filesDir/bpp.log, $filesDir/bpp_dump.txt, logcat"
  Write-Host "  -> $Output"
  return
}

$adb = Need-Adb
if (-not $adb) { exit 1 }

New-Item -ItemType Directory -Force $work | Out-Null
Get-ChildItem $work -File -ErrorAction SilentlyContinue | Remove-Item -Force

Write-Host "== device"
$devices = & $adb devices
$devices | Out-Host
if (-not ($devices -match '^\S+\s+device(?:\s|$)')) {
  Write-Host "没有处于 device 状态的设备（检查 USB 调试授权）。" -ForegroundColor Yellow
  exit 1
}
$ready = @($devices | Where-Object { $_ -match '^\S+\s+device(?:\s|$)' } | ForEach-Object { ($_ -split '\s+')[0] })
if (-not $Serial) {
  if ($ready.Count -ne 1) { throw 'More than one device; pass -Serial.' }
  $Serial = $ready[0]
}
if ($Serial -notin $ready) { throw 'Selected device is not connected or authorized.' }
$env:ANDROID_SERIAL = $Serial

& $adb shell getprop ro.product.model        | Out-File "$work\device-model.txt" -Encoding utf8
& $adb shell getprop ro.build.version.release | Out-File "$work\android-release.txt" -Encoding utf8
& $adb shell getprop ro.build.version.sdk     | Out-File "$work\android-sdk.txt" -Encoding utf8
& $adb shell getprop ro.product.cpu.abi       | Out-File "$work\abi.txt" -Encoding utf8
& $adb shell dumpsys package $Package 2>$null | Out-File "$work\package.txt" -Encoding utf8

Write-Host "== mod log"
& $adb shell "test -f $filesDir/bpp.log && echo yes" | Out-File "$work\has-log.txt" -Encoding utf8
foreach ($name in @('bpp.log','bpp_dump.txt','bpp_state.json','bpp_settings.txt')) {
  $content = & $adb exec-out cat "$filesDir/$name" 2>$null
  if ($LASTEXITCODE -ne 0) { $content = & $adb exec-out cat "$legacyDir/$name" 2>$null }
  if ($LASTEXITCODE -eq 0) { $content | Out-File "$work\$name" -Encoding utf8 }
  else { Write-Host "not available: $name" }
}
& $adb shell "ls -la $filesDir" 2>&1 | Out-File "$work\files-dir.txt" -Encoding utf8

Write-Host "== logcat"
& $adb logcat -d -s BPP:V AndroidRuntime:E DEBUG:E 2>&1 | Out-File "$work\logcat.txt" -Encoding utf8

Write-Host "== zip"
if (Test-Path $Output) { Remove-Item $Output -Force }
Compress-Archive -Path "$work\*" -DestinationPath $Output -Force
Write-Host "report: $Output"
Write-Host ""
if (Test-Path "$work\bpp.log") {
  Write-Host "---- bpp.log (first 25 lines) ----"
  Get-Content "$work\bpp.log" -TotalCount 25 | Out-Host
  Write-Host "---- bpp.log (last 10 lines) ----"
  Get-Content "$work\bpp.log" -Tail 10 | Out-Host
} else {
  Write-Host "bpp.log 没取到：mod 可能根本没被加载（先换 -libmain 变体再试）。" -ForegroundColor Yellow
}
