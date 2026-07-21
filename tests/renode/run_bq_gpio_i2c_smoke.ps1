$ErrorActionPreference = "Stop"

$renode = if ([string]::IsNullOrWhiteSpace($env:BMS_RENODE_PATH)) {
    "C:\Program Files\Renode\bin\Renode.exe"
} else {
    $env:BMS_RENODE_PATH
}
$script = Join-Path $PSScriptRoot "bms_bq_gpio_i2c_smoke.resc"

Push-Location $PSScriptRoot
try {
    & $renode --console --disable-xwt $script
} finally {
    Pop-Location
}
