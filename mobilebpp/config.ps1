# Shared path configuration for all BPP scripts.
# Everything is derived from this file's location, so the project can live in any
# directory. Override with environment variables when a dependency sits elsewhere:
#   BPP_ROOT        project root (defaults to the parent of mobilebpp)
#   BPP_NDK         Android NDK root
#   BPP_JDK         JDK root (needs bin\keytool.exe)
#   BPP_BUILDTOOLS  Android build-tools root (needs zipalign.exe, apksigner.bat)
#   BPP_STAGE       scratch directory for ELF/APK staging (defaults to %TEMP%\bppwork)
#   BPP_PYTHON      python interpreter (defaults to "python" from PATH)

$BppDir = $PSScriptRoot                                  # ...\mobilebpp
$BppRoot = if ($env:BPP_ROOT) { $env:BPP_ROOT } else { Split-Path -Parent $BppDir }

$BppNdk = if ($env:BPP_NDK) { $env:BPP_NDK } else { Join-Path $BppRoot "tools\ndk\android-ndk-r27d" }
$BppJdk = if ($env:BPP_JDK) { $env:BPP_JDK } else { Join-Path $BppRoot "tools\jdk\jdk-21.0.12.1+1" }
$BppBt = if ($env:BPP_BUILDTOOLS) { $env:BPP_BUILDTOOLS } else { Join-Path $BppRoot "tools\buildtools\android-15" }
$BppStage = if ($env:BPP_STAGE) { $env:BPP_STAGE } else { Join-Path $env:TEMP "bppwork" }
# Pick a python that can import lief (needed for ELF injection).
$BppPy = $null
$pyCandidates = @()
if ($env:BPP_PYTHON) { $pyCandidates += $env:BPP_PYTHON }
$pyCandidates += "python"
$dshPy = Get-ChildItem "$env:USERPROFILE\.dsh\dsh-runtimes\*\dependencies\python\python.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($dshPy) { $pyCandidates += $dshPy.FullName }
foreach ($cand in $pyCandidates) {
    & $cand -c "import lief" 2>$null
    if ($LASTEXITCODE -eq 0) { $BppPy = $cand; break }
}
if (-not $BppPy) {
    $BppPy = if ($env:BPP_PYTHON) { $env:BPP_PYTHON } else { "python" }
    Write-Warning "no python with lief found; set BPP_PYTHON or: pip install lief"
}
Write-Verbose "BPP python: $BppPy"

function Assert-BppPath($path, $what) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "missing $what`: $path  (set the matching BPP_* environment variable)"
    }
}
