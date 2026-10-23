# Build libbpp.so for arm64-v8a using the NDK clang toolchain.
param(
  [string]$Out = "build/libbpp.so",
  [switch]$Clean
)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\config.ps1"
$ndk   = $BppNdk
$tc    = "$ndk\toolchains\llvm\prebuilt\windows-x86_64\bin"
$clang = "$tc\clang.exe"
$clangxx = "$tc\clang++.exe"
$sh    = Join-Path $BppRoot "third_party\shadowhook\shadowhook\src\main\cpp"
$imgui = Join-Path $BppRoot "third_party\imgui"
$native = "$PSScriptRoot\native"
$buildDir = "$PSScriptRoot\build"
$obj = "$buildDir\obj"
if ($Clean -and (Test-Path $obj)) {
  $resolvedObj = [IO.Path]::GetFullPath($obj)
  if (-not $resolvedObj.StartsWith([IO.Path]::GetFullPath($PSScriptRoot) + [IO.Path]::DirectorySeparatorChar)) { throw 'invalid object directory' }
  Remove-Item -LiteralPath $resolvedObj -Recurse -Force
}
New-Item -ItemType Directory -Force $obj | Out-Null
& pwsh -NoProfile -File "$PSScriptRoot\tools\build_dex.ps1"
if ($LASTEXITCODE -ne 0) { throw 'Java bridge generation failed' }
& $BppPy "$PSScriptRoot\tools\embed_assets.py"
if ($LASTEXITCODE -ne 0) { throw 'Web asset generation failed' }

$TARGET = "--target=aarch64-linux-android26"
$INCS = @("-I$native", "-I$sh\include", "-I$sh", "-I$sh\arch\arm64", "-I$sh\common",
          "-I$sh\third_party\xdl", "-I$sh\third_party\bsd", "-I$sh\third_party\lss",
          "-I$imgui", "-I$imgui\backends")

$cppFiles = @(Get-ChildItem "$native\*.cpp" | ForEach-Object { $_.FullName })
$cppFiles += @("$imgui\imgui.cpp", "$imgui\imgui_draw.cpp", "$imgui\imgui_tables.cpp",
               "$imgui\imgui_widgets.cpp", "$imgui\backends\imgui_impl_opengl3.cpp")

$cFiles = @(Get-ChildItem "$sh\*.c" | ForEach-Object { $_.FullName })
$cFiles += @(Get-ChildItem "$sh\arch\arm64\*.c" | ForEach-Object { $_.FullName })
$cFiles += @(Get-ChildItem "$sh\common\*.c" | ForEach-Object { $_.FullName })
$cFiles += @(Get-ChildItem "$sh\third_party\xdl\*.c" | ForEach-Object { $_.FullName })
$asmFiles = @("$sh\arch\arm64\sh_glue.S")

$objects = New-Object System.Collections.Generic.List[string]

function Invoke-Compile([string]$compiler, [string]$file, [string[]]$extra) {
  $name = ($file -replace '[\\/:]', '_') + ".o"
  $o = Join-Path $obj $name
  $cmdArgs = @($TARGET, "-fPIC", "-O2", "-DANDROID", "-ffunction-sections", "-fdata-sections") + $INCS + $extra + @("-c", $file, "-o", $o)
  Write-Host "CC $file"
  & $compiler @cmdArgs 2>&1 | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "compile failed: $file" }
  $objects.Add($o) | Out-Null
}

foreach ($f in $cppFiles) {
  $warnFlags = @()
  if ($f -like "*\native\*") { $warnFlags = @("-Wall", "-Wextra") }
  Invoke-Compile $clangxx $f (@("-std=c++17", "-DIMGUI_IMPL_OPENGL_ES3",
    "-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS", "-Wno-unused-parameter", "-Wno-missing-field-initializers") + $warnFlags)
}
foreach ($f in $cFiles) { Invoke-Compile $clang $f @("-std=c11", "-Wno-everything") }
foreach ($f in $asmFiles) {
  $o = Join-Path $obj (($f -replace '[\\/:]', '_') + ".o")
  Write-Host "AS $f"
  $asmArgs = @($TARGET, "-fPIC", "-DANDROID") + $INCS + @("-c", $f, "-o", $o)
  & $clang @asmArgs 2>&1 | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "asm failed: $f" }
  $objects.Add($o) | Out-Null
}

New-Item -ItemType Directory -Force $buildDir | Out-Null
$outPath = Join-Path $buildDir (Split-Path -Leaf $Out)
Write-Host "LINK $outPath"
$linkArgs = @($TARGET, "-shared", "-o", $outPath, "-Wl,-soname,libbpp.so",
              "-Wl,--gc-sections", "-static-libstdc++", "-Wl,-z,max-page-size=16384") + $objects.ToArray() +
            @("-llog", "-landroid", "-lEGL", "-lGLESv3", "-lm", "-ldl")
& $clangxx @linkArgs 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) { throw "link failed" }
$f = Get-Item $outPath
Write-Host ("OK {0} ({1:N0} bytes)" -f $f.FullName, $f.Length)
