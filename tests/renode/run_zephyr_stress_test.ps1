param(
    [switch]$SkipBuild,
    [double]$DurationSeconds = 2.5,
    [switch]$CaptureCan,
    [string]$RenodePath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$renode = $RenodePath
if ([string]::IsNullOrWhiteSpace($renode)) { $renode = $env:BMS_RENODE_PATH }
if ([string]::IsNullOrWhiteSpace($renode)) { $renode = "C:\Program Files\Renode\bin\Renode.exe" }
$renode = (Resolve-Path -LiteralPath $renode).Path
$artifactDir = Join-Path $PSScriptRoot "artifacts"
$logPath = Join-Path $artifactDir "zephyr_stress.log"
$rescPath = Join-Path $artifactDir "zephyr_stress.resc"
$consolePath = Join-Path $artifactDir "zephyr_renode_stress.log"
$reportPath = Join-Path $artifactDir "zephyr_stress_report.json"
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
    $previousOverlay = $env:BMS_EXTRA_OVERLAY
    $env:BMS_EXTRA_OVERLAY = $null
    try { & $firmwareBuild -TestInstrumentation } finally { $env:BMS_EXTRA_OVERLAY = $previousOverlay }
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $logPath, $rescPath, $consolePath, $reportPath
$logTarget = $logPath.Replace("\", "\\")
$canCaptureCommand = ""
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
    $canCaptureCommand = "emulation LogToWireshark canHub"
}
$resc = @"
:name: BMS Zephyr pressure and scheduling test

using sysbus

mach create "bms_zephyr_stress"
machine LoadPlatformDescription @platforms/cpus/stm32f103.repl
machine LoadPlatformDescription @$platformAddons
machine LoadPlatformDescriptionFromString "bq76940: Mocks.DummyI2CSlave @ i2c1 0x08"
emulation CreateCANHub "canHub"
connector Connect can1 canHub
$canCaptureCommand

include @$bqModel
include @$smokeHooks
sysbus LoadELF @$firmwareElf
setup_bq76940_i2c "sysbus.i2c1.bq76940" "$scenarioPath"
bq76940_i2c_self_test "sysbus.i2c1.bq76940"
setup_bq76940_gpio_i2c "sysbus.i2c1.bq76940"

sysbus.usart1 CreateFileBackend @$logTarget
boot_firmware_from_vector_table 0x08000000
logLevel 3
emulation RunFor "$DurationSeconds"
assert_bq76940_gpio_i2c_firmware_path "sysbus.i2c1.bq76940"
sysbus.usart1 CloseFileBackend @$logTarget
"@
Set-Content -LiteralPath $rescPath -Encoding ASCII -Value $resc
if ($CaptureCan) {
    # Keep Wireshark from inheriting the console pipeline handle and holding
    # the stress runner open after Renode has exited.
    $stdoutPath = "$consolePath.stdout"
    $stderrPath = "$consolePath.stderr"
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    $renodeProcess = Start-Process -FilePath $renode -ArgumentList @(
        "--console", "--disable-gui", "-p", $rescPath, "-e", "quit"
    ) -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
    $renodeProcess.WaitForExit()
    $renodeExitCode = $renodeProcess.ExitCode
    $renodeText = ""
    if (Test-Path -LiteralPath $stdoutPath) { $renodeText += Get-Content -Raw -Path $stdoutPath }
    if (Test-Path -LiteralPath $stderrPath) { $renodeText += Get-Content -Raw -Path $stderrPath }
    Set-Content -LiteralPath $consolePath -Encoding UTF8 -Value $renodeText
    $renodeText | Write-Output
    if ($renodeExitCode -ne 0) { exit $renodeExitCode }
} else {
    & $renode --console --disable-gui -p $rescPath -e "quit" 2>&1 |
        Tee-Object -FilePath $consolePath | Write-Output
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if (-not (Test-Path -LiteralPath $logPath)) { throw "Stress test did not create UART log" }
[string]$content = Get-Content -Raw -Encoding ASCII -Path $logPath
[string]$console = Get-Content -Raw -Encoding ASCII -Path $consolePath
$failures = New-Object System.Collections.Generic.List[string]

if (-not $content.Contains("BMS UART diag ready")) { $failures.Add("UART banner missing") }
if (-not $content.Contains("BMS_CAN_STATE") -or -not [regex]::IsMatch($content, 'BMS_CAN_STATE .*"setup_result":0')) { $failures.Add("CAN setup did not pass") }
$canTx = [regex]::Matches($content, 'BMS_CAN_TX .*"result":0').Count
if ($canTx -lt 7) { $failures.Add("CAN TX success count=$canTx, expected >=7") }
if (-not [regex]::IsMatch($content, 'BMS_WDT_STATE .*"setup_ok":1')) { $failures.Add("WDT setup telemetry missing") }
if (-not [regex]::IsMatch($content, 'BMS_WDT_STATE .*"feed_count":[1-9]')) { $failures.Add("WDT feed telemetry missing") }
if ($console.Contains("ASSERTION FAIL") -or $console.Contains("Kernel panic") -or $console.Contains("HardFault")) { $failures.Add("kernel/assertion fault observed") }

$timings = [regex]::Matches($content, 'BMS_SAMPLE_TIMING .*"duration_ms":(\d+)')
$maxSampleMs = if ($timings.Count) { ($timings | ForEach-Object { [int]$_.Groups[1].Value } | Measure-Object -Maximum).Maximum } else { -1 }
if ($timings.Count -eq 0) { $failures.Add("sample timing telemetry missing") }
if ($maxSampleMs -gt 100) { $failures.Add("strict sample deadline exceeded: max=${maxSampleMs}ms") }

$overruns = [regex]::Matches($content, 'BMS_SCHED_OVERRUN ')
if ($overruns.Count -gt 0) { $failures.Add("scheduler overrun events=$($overruns.Count)") }
$stackValues = [regex]::Matches($content, '"stack_utilization_percent":(\d+)') |
    ForEach-Object { [int]$_.Groups[1].Value }
$maxStack = if ($stackValues.Count) { ($stackValues | Measure-Object -Maximum).Maximum } else { -1 }
$remainingValues = [regex]::Matches($content, '"remaining_bytes":(\d+)') |
    ForEach-Object { [int]$_.Groups[1].Value }
$minRemaining = if ($remainingValues.Count) { ($remainingValues | Measure-Object -Minimum).Minimum } else { -1 }
if ($maxStack -lt 0) { $failures.Add("thread stack telemetry missing") }
if ($maxStack -ge 100) { $failures.Add("thread stack overflow observed") }
$schedMatches = [regex]::Matches($content, 'BMS_SCHED .*"runtime_stats_available":(true|false)')
$runtimeStats = if ($schedMatches.Count) { $schedMatches[$schedMatches.Count - 1].Groups[1].Value -eq "true" } else { $false }

$status = if ($failures.Count -eq 0) { "PASS" } else { "FAIL" }
$report = [ordered]@{
    test = "zephyr_pressure_and_scheduling"
    status = $status
    duration_seconds = $DurationSeconds
    sample_count = $timings.Count
    max_sample_ms = $maxSampleMs
    scheduler_overrun_count = $overruns.Count
    max_stack_utilization_percent = $maxStack
    min_stack_remaining_bytes = $minRemaining
    can_tx_success_count = $canTx
    runtime_stats_available = $runtimeStats
    failures = @($failures)
    uart_log = $logPath
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding ASCII
Write-Output "Zephyr stress test ${status}: samples=$($timings.Count) max_sample_ms=$maxSampleMs max_stack=$maxStack% min_stack_remaining=$minRemaining bytes can_tx=$canTx runtime_stats=$runtimeStats"
Write-Output "Report: $reportPath"
if ($failures.Count -gt 0) { $failures | ForEach-Object { Write-Output "FAIL: $_" }; exit 2 }
