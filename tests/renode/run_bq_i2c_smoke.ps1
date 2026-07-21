$ErrorActionPreference = "Stop"

$renode = if ([string]::IsNullOrWhiteSpace($env:BMS_RENODE_PATH)) {
    "C:\Program Files\Renode\bin\Renode.exe"
} else {
    $env:BMS_RENODE_PATH
}
$script = Join-Path $PSScriptRoot "bms_bq_i2c_smoke.resc"

if (-not (Test-Path -LiteralPath $renode)) {
    throw "Renode.exe not found at $renode"
}

Push-Location $PSScriptRoot
try {
    & $renode --console --disable-gui -p $script -e "peripherals" -e "quit"
} finally {
    Pop-Location
}
