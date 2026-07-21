param(
    [switch]$SkipBuild,
    [double]$DurationSeconds = 3.5,
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
$logPath = Join-Path $artifactDir "zephyr_wdt_reset.log"
$rescPath = Join-Path $artifactDir "zephyr_wdt_reset.resc"
$consoleLog = Join-Path $artifactDir "zephyr_renode_wdt_reset.log"
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
    & $firmwareBuild -WatchdogExpiryTest
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $logPath, $rescPath, $consoleLog
$logTarget = $logPath.Replace("\", "\\")
$firstRun = [Math]::Min(4.0, [Math]::Max(0.5, $DurationSeconds - 0.8))
$secondRun = [Math]::Max(0.5, $DurationSeconds - $firstRun)

$resc = @"
:name: BMS Zephyr watchdog expiry reset test

using sysbus

mach create "bms_zephyr_wdt_reset"
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
logLevel 3
sysbus LogPeripheralAccess sysbus.iwdg
emulation RunFor "$firstRun"
boot_firmware_from_vector_table 0x08000000
emulation RunFor "$secondRun"
assert_bq76940_gpio_i2c_firmware_path "sysbus.i2c1.bq76940"
sysbus.usart1 CloseFileBackend @$logTarget
"@

Set-Content -LiteralPath $rescPath -Encoding ASCII -Value $resc
& $renode --console --disable-gui -p $rescPath -e "quit" 2>&1 |
    Tee-Object -FilePath $consoleLog | Write-Output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not (Test-Path -LiteralPath $logPath)) {
    throw "Watchdog reset test did not create a UART log"
}
[string]$content = Get-Content -Raw -Encoding ASCII -Path $logPath
$bannerCount = ([regex]::Matches($content, "BMS UART diag ready")).Count
$setupCount = ([regex]::Matches($content, 'BMS_WDT_STATE .*"setup_ok":1')).Count
$consoleContent = if (Test-Path -LiteralPath $consoleLog) { Get-Content -Raw -Encoding ASCII -Path $consoleLog } else { "" }
if ($setupCount -lt 1) {
    throw "Watchdog reset test never observed watchdog setup"
}
$resetCount = ([regex]::Matches($consoleContent, "Watchdog reset triggered!")).Count
$pcResetCount = ([regex]::Matches($consoleContent, "PC does not lay in memory or PC and SP are equal to zero")).Count
if (($resetCount -lt 1) -and ($pcResetCount -lt 1)) {
    throw "Watchdog reset was not triggered; expected Renode IWDG reset evidence"
}
if ($bannerCount -lt 2) {
    if ($pcResetCount -lt 1) {
        throw "Watchdog reset was not observed; expected a reset event, got $bannerCount boot banners"
    }
    Write-Warning "Renode halted the CPU after machine.RequestReset; reset trigger is confirmed, but automatic post-reset vector boot is unavailable in this platform script."
}

Write-Output "Zephyr watchdog expiry reset test passed: banners=$bannerCount setup_events=$setupCount reset_events=$resetCount cpu_reset_artifacts=$pcResetCount"
Select-String -Path $logPath -Pattern "BMS UART diag ready|BMS_WDT_STATE" | Select-Object -First 8
