param(
    [switch]$SkipBuild,
    [double]$DurationSeconds = 2.0,
    [string]$RenodePath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$renode = $RenodePath
if ([string]::IsNullOrWhiteSpace($renode)) {
    $renode = $env:BMS_RENODE_PATH
}
if ([string]::IsNullOrWhiteSpace($renode)) {
    $renode = "C:\Program Files\Renode\bin\Renode.exe"
}
$renode = (Resolve-Path -LiteralPath $renode).Path
$artifactDir = Join-Path $PSScriptRoot "artifacts"
$logPath = Join-Path $artifactDir "zephyr_uart_rx.log"
$rescPath = Join-Path $artifactDir "zephyr_uart_rx.resc"
$consoleLog = Join-Path $artifactDir "zephyr_renode_uart_rx.log"
$firmwareBuild = Join-Path $repoRoot "firmware\build.ps1"
$firmwareElf = (Join-Path $repoRoot `
        "firmware\build\zephyr\zephyr.elf").Replace("\", "/")
$scenarioPath = (Resolve-Path (Join-Path $repoRoot `
        "tests\scenarios\normal_15s_shipping.json")).Path.Replace("\", "/")
$platformAddons = (Resolve-Path (Join-Path $PSScriptRoot `
        "platforms\bms_stm32f103_bq76940_addons.repl")).Path.Replace("\", "/")
$bqModel = (Resolve-Path (Join-Path $PSScriptRoot `
        "peripherals\bq76940_i2c.py")).Path.Replace("\", "/")
$smokeHooks = (Resolve-Path (Join-Path $PSScriptRoot `
        "peripherals\firmware_smoke_hooks.py")).Path.Replace("\", "/")

if (-not $SkipBuild) {
    & $firmwareBuild -TestInstrumentation
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $logPath, $rescPath, $consoleLog
$logTarget = $logPath.Replace("\", "\\")

$resc = @"
:name: BMS Zephyr UART RX command test

using sysbus

mach create "bms_zephyr_uart_rx"
machine LoadPlatformDescription @platforms/cpus/stm32f103.repl
machine LoadPlatformDescription @$platformAddons
machine LoadPlatformDescriptionFromString "bq76940: Mocks.DummyI2CSlave @ i2c1 0x08"
emulation CreateCANHub "canHub"
connector Connect can1 canHub

include @$bqModel
include @$smokeHooks
sysbus LoadELF @$firmwareElf
setup_bq76940_i2c "sysbus.i2c1.bq76940" "$scenarioPath"
bq76940_i2c_self_test "sysbus.i2c1.bq76940"
setup_bq76940_gpio_i2c "sysbus.i2c1.bq76940"

sysbus.usart1 CreateFileBackend @$logTarget
boot_firmware_from_vector_table 0x08000000
emulation RunFor "0.8"
sysbus.usart1 WriteChar 88
sysbus.usart1 WriteChar 83
sysbus.usart1 WriteChar 82
logLevel 3
emulation RunFor "$DurationSeconds"
assert_bq76940_gpio_i2c_firmware_path "sysbus.i2c1.bq76940"
sysbus.usart1 CloseFileBackend @$logTarget
"@

Set-Content -LiteralPath $rescPath -Encoding ASCII -Value $resc
& $renode --console --disable-gui -p $rescPath -e "quit" 2>&1 |
    Tee-Object -FilePath $consoleLog | Write-Output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not (Test-Path -LiteralPath $logPath)) {
    throw "UART RX test did not create a UART log"
}
[string]$content = Get-Content -Raw -Encoding ASCII -Path $logPath
if (-not ($content -like "*BMS UART diag ready*")) {
    throw "UART RX test did not reach the diagnostic task"
}
if (-not ($content -like "*ERR command*")) {
    throw "UART RX invalid-command path was not exercised"
}
if (-not ($content -like "*SNAP version=*")) {
    throw "UART RX S command did not produce a snapshot"
}
if (-not ($content -like "*AFE_REG addr=00 value=*")) {
    throw "UART RX R command did not produce AFE register data"
}
if (-not ($content -like "*AFE_REG addr=59 value=*")) {
    throw "UART RX R command did not complete the AFE register range"
}

Write-Output "Zephyr UART RX command test passed: $logPath"
Select-String -Path $logPath -Pattern "ERR command|SNAP version=|AFE_REG addr=" | Select-Object -First 8
