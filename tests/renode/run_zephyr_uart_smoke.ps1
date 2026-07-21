param(
    [ValidateSet("all", "normal", "overvoltage", "balance", "undervoltage", "ntc_open", "ntc_short")]
    [string]$Scenario = "all",
    [switch]$SkipBuild,
    [double]$DurationSeconds = 1.3,
    [switch]$TransportOnly,
    [switch]$TraceCan,
    [switch]$CaptureCan,
    [switch]$CanInitWorkaround,
    [string]$RenodePath = ""
)

$ErrorActionPreference = "Stop"

# Zephyr BMS firmware smoke test.
# Keep this script ASCII because Windows PowerShell 5.1 reads .ps1 files with
# the system ANSI code page unless the file has a BOM. Firmware source comments
# remain Chinese; this test harness stays encoding-safe.

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
$firmwareBuild = Join-Path $repoRoot "firmware\build.ps1"
$firmwareElf = (Join-Path $repoRoot `
        "firmware\build\zephyr\zephyr.elf").Replace("\", "/")
$platformAddons = (Resolve-Path (Join-Path $PSScriptRoot `
        "platforms\bms_stm32f103_bq76940_addons.repl")).Path.Replace("\", "/")
$bqModel = (Resolve-Path (Join-Path $PSScriptRoot `
        "peripherals\bq76940_i2c.py")).Path.Replace("\", "/")
$smokeHooks = (Resolve-Path (Join-Path $PSScriptRoot `
        "peripherals\firmware_smoke_hooks.py")).Path.Replace("\", "/")

if (-not (Test-Path -LiteralPath $renode)) {
    throw "Renode.exe not found at $renode"
}

# Formal smoke uses the shipping software-IIC devicetree path. Clear an ambient
# overlay so a developer shell cannot silently switch this run to hardware I2C.
if (-not $SkipBuild) {
    $previousOverlay = $env:BMS_EXTRA_OVERLAY
    $env:BMS_EXTRA_OVERLAY = $null
    try {
        & $firmwareBuild -TestInstrumentation
    } finally {
        $env:BMS_EXTRA_OVERLAY = $previousOverlay
    }
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
Get-ChildItem -Path $artifactDir -Filter "zephyr_uart*.log*" -File -ErrorAction SilentlyContinue |
    Remove-Item -Force

$canTraceCommand = ""
$canTraceLevelCommand = ""
$canCaptureCommand = ""
$canInitWorkaroundCommand = ""
if ($TraceCan) {
    $canTraceCommand = "sysbus LogPeripheralAccess sysbus.can1"
    $canTraceLevelCommand = "logLevel -1"
}
if ($CaptureCan) {
    $wiresharkPath = "C:\Program Files\Wireshark\Wireshark.exe"
    $renodeConfig = Join-Path $env:APPDATA "renode\config"
    if (Test-Path -LiteralPath $renodeConfig) {
        $configuredPath = Select-String -LiteralPath $renodeConfig -Pattern '^wireshark-path\s*=\s*(.+)$' |
            Select-Object -First 1
        if ($configuredPath) {
            $wiresharkPath = $configuredPath.Matches[0].Groups[1].Value.Trim()
        }
    }
    if (-not (Test-Path -LiteralPath $wiresharkPath)) {
        throw "CAN capture requested but Wireshark was not found at $wiresharkPath"
    }
    # Captures actual CAN frames on the hub; LogPeripheralAccess above remains
    # available separately for register-level init diagnostics.
    $canCaptureCommand = "emulation LogToWireshark canHub"
}
if ($CanInitWorkaround) {
    # Validation only: mimic the corrected STMCAN model by clearing SLEEP
    # before Zephyr writes INRQ. Remove after the Renode model is rebuilt.
    $canInitWorkaroundCommand = "sysbus WriteDoubleWord 0x40006400 0x00010000"
}

function Invoke-ZephyrRenodeSmoke {
    param(
        [string]$Name,
        [string]$Scenario,
        [string]$ExpectedPack,
        [string]$ExpectedCells,
        [string]$ExpectedCurrent,
        [string]$ExpectedSocRegex,
        [string]$ExpectedFaultRegex,
        [string]$ExpectedBalanceRegex,
        [string]$ExpectedPowerRegex,
        [string]$ExpectedTempRegex = "temp_cdeg=\[(249[0-9]|250[0-9]|2510|260[0-9]|261[0-9]),(249[0-9]|250[0-9]|2510|260[0-9]|261[0-9])\]",
        [string]$ExpectedTempValid = "temp_valid=3",
        [int]$ExpectedLedFault = 0
    )

    $caseLog = Join-Path $artifactDir "zephyr_uart_$Name.log"
    $caseScript = Join-Path $artifactDir "zephyr_uart_$Name.resc"
    $scenarioPath = (Resolve-Path (Join-Path $repoRoot $Scenario)).Path.Replace("\", "/")
    $logPath = $caseLog.Replace("\", "\\")

    $resc = @"
:name: BMS Zephyr UART smoke $Name

using sysbus

mach create "bms_zephyr_uart_$Name"
machine LoadPlatformDescription @platforms/cpus/stm32f103.repl
machine LoadPlatformDescription @$platformAddons
machine LoadPlatformDescriptionFromString "bq76940: Mocks.DummyI2CSlave @ i2c1 0x08"
emulation CreateCANHub "canHub"
connector Connect can1 canHub
$canTraceCommand
$canCaptureCommand

include @$bqModel
include @$smokeHooks
sysbus LoadELF @$firmwareElf
setup_bq76940_i2c "sysbus.i2c1.bq76940" "$scenarioPath"
bq76940_i2c_self_test "sysbus.i2c1.bq76940"
setup_bq76940_gpio_i2c "sysbus.i2c1.bq76940"

sysbus.usart1 CreateFileBackend @$logPath
boot_firmware_from_vector_table 0x08000000
$canInitWorkaroundCommand

logLevel 3
$canTraceLevelCommand
emulation RunFor "$DurationSeconds"
assert_bq76940_gpio_i2c_firmware_path "sysbus.i2c1.bq76940"
sysbus.usart1 CloseFileBackend @$logPath
"@
    Set-Content -LiteralPath $caseScript -Encoding ASCII -Value $resc

    $renodeConsoleLog = Join-Path $artifactDir "zephyr_renode_$Name.log"
    if ($CaptureCan) {
        # Wireshark inherits Renode's output handle when the pipeline form is
        # used, so the test shell waits forever after Renode has quit. Keep
        # capture in a separate process with redirected output instead.
        $renodeStdout = "$renodeConsoleLog.stdout"
        $renodeStderr = "$renodeConsoleLog.stderr"
        Remove-Item -LiteralPath $renodeStdout, $renodeStderr -Force -ErrorAction SilentlyContinue
        $renodeProcess = Start-Process -FilePath $renode -ArgumentList @(
            "--console", "--disable-gui", "-p", $caseScript, "-e", "quit"
        ) -RedirectStandardOutput $renodeStdout -RedirectStandardError $renodeStderr -PassThru
        $renodeProcess.WaitForExit()
        $renodeExitCode = $renodeProcess.ExitCode
        $renodeText = ""
        if (Test-Path -LiteralPath $renodeStdout) { $renodeText += Get-Content -Raw -Path $renodeStdout }
        if (Test-Path -LiteralPath $renodeStderr) { $renodeText += Get-Content -Raw -Path $renodeStderr }
        Set-Content -LiteralPath $renodeConsoleLog -Encoding UTF8 -Value $renodeText
        $renodeText | Write-Output
        if ($renodeExitCode -ne 0) {
            exit $renodeExitCode
        }
    } else {
        & $renode --console --disable-gui -p $caseScript -e "quit" 2>&1 |
            Tee-Object -FilePath $renodeConsoleLog | Write-Output
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
    $renodeText = Get-Content -Raw -Path $renodeConsoleLog
    if ($renodeText.Contains("There was an error executing command") -or
        ($renodeText.Contains("Command '") -and $renodeText.Contains(" failed"))) {
        throw "Renode monitor command failed. Case=$Name"
    }

    if (-not (Test-Path -LiteralPath $caseLog)) {
        throw "UART log was not created: $caseLog"
    }

    if ($TransportOnly) {
        Write-Output "Zephyr software-IIC transport smoke passed [$Name]"
        return
    }

    [string]$content = Get-Content -Raw -Encoding ASCII -Path $caseLog
    if ([string]::IsNullOrWhiteSpace($content)) {
        throw "UART smoke output was not observed. Case=$Name Log content: $content"
    }
    if (-not ($content -like "*BMS UART diag ready*")) {
        throw "UART diag banner was not observed. Case=$Name Log content: $content"
    }
    $snapCount = ([regex]::Matches($content, "SNAP version=")).Count
    if ($snapCount -lt 2) {
        throw "event-driven UART snapshots were not observed. Case=$Name Log content: $content"
    }
    if (-not (($content.Contains("pack_mv=")) -and ($content.Contains("cells_mv=[")) -and ($content.Contains("current_ma=")) -and ($content.Contains("temp_cdeg=[")))) {
        throw "UART snapshot fields were not observed. Case=$Name Log content: $content"
    }
    if (-not (($content.Contains("soc_0p1=")) -and ($content.Contains("soh_0p1=")) -and ($content.Contains("sop_chg_ma=")) -and ($content.Contains("sop_dsg_ma=")))) {
        throw "energy algorithm fields were not observed. Case=$Name Log content: $content"
    }
    if (-not (($content.Contains("fault_bits=")) -and ($content.Contains("afe_status=")) -and ($content.Contains("bal_req=")) -and ($content.Contains("bal_mask=")) -and ($content.Contains("bal_delta_mv=")) -and ($content.Contains("pwr_mode=")))) {
        throw "fault/balance/power fields were not observed. Case=$Name Log content: $content"
    }
    if (-not ($content.Contains($ExpectedPack))) {
        throw "expected BQ76940 scenario pack voltage was not observed. Case=$Name Log content: $content"
    }
    if (-not ($content.Contains($ExpectedCells))) {
        throw "expected BQ76940 scenario cell voltages were not observed. Case=$Name Log content: $content"
    }
    if (-not ($content.Contains($ExpectedCurrent))) {
        throw "expected BQ76940 scenario current was not observed. Case=$Name Log content: $content"
    }
    if (-not ([regex]::IsMatch($content, $ExpectedTempRegex))) {
        throw "expected BQ76940 scenario temperature was not observed. Case=$Name Log content: $content"
    }
    if (-not ($content.Contains($ExpectedTempValid))) {
        throw "expected NTC validity marker $ExpectedTempValid was not observed. Case=$Name Log content: $content"
    }
    if (-not ([regex]::IsMatch($content, $ExpectedSocRegex))) {
        throw "expected OCV-based SOC was not observed. Case=$Name Log content: $content"
    }
    if (-not ([regex]::IsMatch($content, $ExpectedFaultRegex))) {
        throw "expected fault state was not observed. Case=$Name Log content: $content"
    }
    if (-not ([regex]::IsMatch($content, $ExpectedBalanceRegex))) {
        throw "expected balance state was not observed. Case=$Name Log content: $content"
    }
    if (-not ([regex]::IsMatch($content, $ExpectedPowerRegex))) {
        throw "expected power/switch state was not observed. Case=$Name Log content: $content"
    }
    if (-not ([regex]::IsMatch($content, 'BMS_LED_STATE .*"fault":' + $ExpectedLedFault))) {
        throw "expected LED fault state $ExpectedLedFault was not observed. Case=$Name Log content: $content"
    }
    $wdtStateCount = ([regex]::Matches($content, 'BMS_WDT_STATE ')).Count
    if (-not ([regex]::IsMatch($content, 'BMS_WDT_STATE .*"setup_ok":1')) -or
        ($wdtStateCount -lt 2)) {
        throw "watchdog setup/feed telemetry was not observed. Case=$Name Log content: $content"
    }
    if (-not ([regex]::IsMatch($content, 'BMS_CAN_STATE .*"setup_result":0.*"filter_result":[0-9]+'))) {
        throw "CAN controller/filter setup was not successful. Case=$Name Log content: $content"
    }
    if (-not ([regex]::IsMatch($content, 'BMS_THREAD_STACK '))) {
        throw "thread stack telemetry was not observed. Case=$Name Log content: $content"
    }
    if (-not ([regex]::IsMatch($content, 'BMS_SCHED '))) {
        throw "scheduler telemetry was not observed. Case=$Name Log content: $content"
    }
    $timings = [regex]::Matches($content, 'BMS_SAMPLE_TIMING .*"duration_ms":(\d+)')
    if ($timings.Count -eq 0) {
        throw "sample timing telemetry was not observed. Case=$Name Log content: $content"
    }
    $maxSampleMs = ($timings | ForEach-Object { [int]$_.Groups[1].Value } | Measure-Object -Maximum).Maximum
    if ($maxSampleMs -gt 100) {
        throw "sample execution exceeded 100 ms period. Case=$Name MaxSampleMs=$maxSampleMs Log content: $content"
    }
    $canTxCount = ([regex]::Matches($content, 'BMS_CAN_TX ')).Count
    if ($canTxCount -lt 7 -or -not ([regex]::IsMatch($content, 'BMS_CAN_TX .*"result":0'))) {
        throw "CAN transmit telemetry was not successful. Case=$Name Count=$canTxCount Log content: $content"
    }
    $overruns = [regex]::Matches($content, 'BMS_SCHED_OVERRUN .*"timestamp_ms":(\d+)')
    if ($overruns.Count -gt 1 -or ($overruns.Count -eq 1 -and [int]$overruns[0].Groups[1].Value -ge 1000)) {
        throw "periodic scheduler overrun was observed. Case=$Name Log content: $content"
    }

    Write-Output "Zephyr UART smoke passed [$Name]: $caseLog"
    Write-Output (($content -split "`r?`n" | Select-Object -First 4) -join "`n")
}

if (($Scenario -eq "all") -or ($Scenario -eq "normal")) {
Invoke-ZephyrRenodeSmoke `
    -Name "normal" `
    -Scenario "tests\scenarios\normal_15s_shipping.json" `
    -ExpectedPack "pack_mv=32855" `
    -ExpectedCells "cells_mv=[3650,3651,3651,3648,3653,3650,3651,3651,3650]" `
    -ExpectedCurrent "current_ma=0" `
    -ExpectedSocRegex "soc_0p1=34[0-9] " `
    -ExpectedFaultRegex "fault_bits=0" `
    -ExpectedBalanceRegex "bal_mask=0" `
    -ExpectedPowerRegex "pwr_mode=0 chg=1 dsg=1" `
    -ExpectedLedFault 0
}

if (($Scenario -eq "all") -or ($Scenario -eq "overvoltage")) {
Invoke-ZephyrRenodeSmoke `
    -Name "overvoltage" `
    -Scenario "tests\scenarios\overvoltage_15s_shipping.json" `
    -ExpectedPack "pack_mv=33414" `
    -ExpectedCells "cells_mv=[3650,3651,4210,3648,3653,3650,3651,3651,3650]" `
    -ExpectedCurrent "current_ma=-1200" `
    -ExpectedSocRegex "soc_0p1=34[0-9] " `
    -ExpectedFaultRegex "fault_bits=1" `
    -ExpectedBalanceRegex "bal_mask=4" `
    -ExpectedPowerRegex "pwr_mode=3 chg=0 dsg=1" `
    -ExpectedLedFault 1
}

if (($Scenario -eq "all") -or ($Scenario -eq "balance")) {
Invoke-ZephyrRenodeSmoke `
    -Name "balance" `
    -Scenario "tests\scenarios\balance_15s_shipping.json" `
    -ExpectedPack "pack_mv=35262" `
    -ExpectedCells "cells_mv=[3899,3899,3989,3899,3980,3899,3899,3899,3899]" `
    -ExpectedCurrent "current_ma=0" `
    -ExpectedSocRegex "soc_0p1=64[0-9] " `
    -ExpectedFaultRegex "fault_bits=0" `
    -ExpectedBalanceRegex "bal_mask=20" `
    -ExpectedPowerRegex "pwr_mode=0 chg=1 dsg=1" `
    -ExpectedLedFault 0
}

if (($Scenario -eq "all") -or ($Scenario -eq "undervoltage")) {
Invoke-ZephyrRenodeSmoke `
    -Name "undervoltage" `
    -Scenario "tests\scenarios\undervoltage_15s_shipping.json" `
    -ExpectedPack "pack_mv=31904" `
    -ExpectedCells "cells_mv=[3650,3651,3651,3648,3653,3650,3651,3651,2699]" `
    -ExpectedCurrent "current_ma=0" `
    -ExpectedSocRegex "soc_0p1=0 " `
    -ExpectedFaultRegex "fault_bits=2" `
    -ExpectedBalanceRegex "bal_mask=0" `
    -ExpectedPowerRegex "pwr_mode=3 chg=1 dsg=0" `
    -ExpectedLedFault 1
}

if (($Scenario -eq "all") -or ($Scenario -eq "ntc_open")) {
Invoke-ZephyrRenodeSmoke `
    -Name "ntc_open" `
    -Scenario "tests\scenarios\ntc_open_15s_shipping.json" `
    -ExpectedPack "pack_mv=32855" `
    -ExpectedCells "cells_mv=[3650,3651,3651,3648,3653,3650,3651,3651,3650]" `
    -ExpectedCurrent "current_ma=0" `
    -ExpectedSocRegex "soc_0p1=34[0-9] " `
    -ExpectedFaultRegex "fault_bits=2048" `
    -ExpectedBalanceRegex "bal_mask=0" `
    -ExpectedPowerRegex "pwr_mode=3 chg=0 dsg=1" `
    -ExpectedTempRegex "temp_cdeg=\[0,250[0-9]\]" `
    -ExpectedTempValid "temp_valid=2" `
    -ExpectedLedFault 1
}

if (($Scenario -eq "all") -or ($Scenario -eq "ntc_short")) {
Invoke-ZephyrRenodeSmoke `
    -Name "ntc_short" `
    -Scenario "tests\scenarios\ntc_short_15s_shipping.json" `
    -ExpectedPack "pack_mv=32855" `
    -ExpectedCells "cells_mv=[3650,3651,3651,3648,3653,3650,3651,3651,3650]" `
    -ExpectedCurrent "current_ma=0" `
    -ExpectedSocRegex "soc_0p1=34[0-9] " `
    -ExpectedFaultRegex "fault_bits=2048" `
    -ExpectedBalanceRegex "bal_mask=0" `
    -ExpectedPowerRegex "pwr_mode=3 chg=0 dsg=1" `
    -ExpectedTempRegex "temp_cdeg=\[0,250[0-9]\]" `
    -ExpectedTempValid "temp_valid=2" `
    -ExpectedLedFault 1
}
