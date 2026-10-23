$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\config.ps1"
$classes = Join-Path $BppDir 'build\java-classes'
$dex = Join-Path $BppDir 'build\dex'
New-Item -ItemType Directory -Force $classes, $dex | Out-Null
& "$BppJdk\bin\javac.exe" --release 8 -encoding UTF-8 -d $classes `
  "$BppDir\java\stubs\android\webkit\JavascriptInterface.java" "$BppDir\java\bpp\Ui.java"
if ($LASTEXITCODE -ne 0) { throw 'Java bridge compilation failed' }
$bridgeClasses = @(Get-ChildItem -LiteralPath "$classes\bpp" -Filter 'Ui*.class' | ForEach-Object { $_.FullName })
& "$BppJdk\bin\java.exe" -cp "$BppBt\lib\d8.jar" com.android.tools.r8.D8 --min-api 26 --output $dex @bridgeClasses
if ($LASTEXITCODE -ne 0) { throw 'DEX generation failed' }
& $BppPy "$PSScriptRoot\embed_assets.py" --dex "$dex\classes.dex"
if ($LASTEXITCODE -ne 0) { throw 'DEX header generation failed' }
