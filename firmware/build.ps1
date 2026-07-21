param(
    [switch]$TestInstrumentation,
    [switch]$WatchdogExpiryTest,
    [switch]$Hardware
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$repoTools = Join-Path $repoRoot "Tools"
$siblingTools = Join-Path (Split-Path -Parent $repoRoot) "Tools"
$toolRoot = if (Test-Path -LiteralPath $repoTools) {
    $repoTools
} elseif (Test-Path -LiteralPath $siblingTools) {
    $siblingTools
} else {
    $repoTools
}
$defaultWorkspace = Join-Path $toolRoot "zephyr-workspace"
$workspace = if ([string]::IsNullOrWhiteSpace($env:ZEPHYR_WORKSPACE)) {
    $defaultWorkspace
} else {
    $env:ZEPHYR_WORKSPACE
}
$zephyrBase = if ([string]::IsNullOrWhiteSpace($env:ZEPHYR_BASE)) {
    Join-Path $workspace "zephyr"
} else {
    $env:ZEPHYR_BASE
}
$pythonScripts = @(
    @(
        (& python -c "import sysconfig; print(sysconfig.get_path('scripts'))").Trim()
        (& python -c "import sysconfig; print(sysconfig.get_path('scripts', 'nt_user'))").Trim()
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Select-Object -Unique
)
$defaultArmGcc = Join-Path `
    $toolRoot `
    "arm-gcc\xpack-arm-none-eabi-gcc-15.2.1-1.1"
$armGcc = if ([string]::IsNullOrWhiteSpace(
        $env:GNUARMEMB_TOOLCHAIN_PATH)) {
    $defaultArmGcc
} else {
    $env:GNUARMEMB_TOOLCHAIN_PATH
}
# Hardware builds go to a separate dir so they never clobber the Renode sim
# build. Sim = HSI 8MHz (app.overlay); hardware = HSE/PLL/72MHz (hw.overlay).
if ($Hardware) {
    $buildDir = Join-Path $PSScriptRoot "build_hw"
} else {
    $buildDir = Join-Path $PSScriptRoot "build"
}

if (-not (Test-Path -LiteralPath (Join-Path $zephyrBase "CMakeLists.txt"))) {
    throw "Zephyr base not found: $zephyrBase"
}
if (-not (Test-Path -LiteralPath (Join-Path $armGcc "bin\arm-none-eabi-gcc.exe"))) {
    throw "ARM GCC not found: $armGcc"
}

$env:PATH = (($pythonScripts + (Join-Path $armGcc "bin") + $env:PATH) -join ";")
$env:ZEPHYR_BASE = $zephyrBase
$env:ZEPHYR_TOOLCHAIN_VARIANT = "gnuarmemb"
$env:GNUARMEMB_TOOLCHAIN_PATH = $armGcc

# Optional extra overlay: the Renode smoke sets BMS_EXTRA_OVERLAY to switch the
# BQ76940 onto hardware I2C for value regression. Normal/real-hardware builds
# leave it unset and use app.overlay (software I2C). Keep this file ASCII:
# Windows PowerShell 5.1 mis-parses non-ASCII .ps1 without a BOM.
$cmakeArgs = @()
$overlayFiles = @()
if ($env:BMS_EXTRA_OVERLAY) {
    $overlayFiles += $env:BMS_EXTRA_OVERLAY
}
if ($Hardware) {
    # hw.overlay switches the clock tree to HSE 8MHz -> PLL x9 -> 72MHz.
    $overlayFiles += "hw.overlay"
}
if ($overlayFiles.Count -gt 0) {
    $cmakeArgs += "-DEXTRA_DTC_OVERLAY_FILE=$($overlayFiles -join ';')"
}
$extraConfFiles = @()
if ($Hardware) {
    # hw.conf enables UART async + DMA (real silicon only; Renode has no DMA model).
    $extraConfFiles += "hw.conf"
}
if ($TestInstrumentation) {
    $extraConfFiles += "test.conf"
}
if ($WatchdogExpiryTest) {
    $extraConfFiles += "test_wdt.conf"
}
if ($extraConfFiles.Count -gt 0) {
    $cmakeArgs += "-DEXTRA_CONF_FILE=$($extraConfFiles -join ';')"
}

if ($cmakeArgs.Count -gt 0) {
    west build -p always -b stm32_min_dev@blue -d $buildDir $PSScriptRoot '--' @cmakeArgs
} else {
    west build -p always -b stm32_min_dev@blue -d $buildDir $PSScriptRoot
}
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$elf = Join-Path $buildDir "zephyr\zephyr.elf"
& (Join-Path $armGcc "bin\arm-none-eabi-size.exe") $elf
