param(
    [switch]$SkipBuild,
    [string]$RenodePath = ""
)

$ErrorActionPreference = "Stop"

$script = Join-Path $PSScriptRoot "run_zephyr_uart_smoke.ps1"
$parameters = @{
    Scenario = "all"
    CanInitWorkaround = $true
    SkipBuild = $SkipBuild
}
if (-not [string]::IsNullOrWhiteSpace($RenodePath)) {
    $parameters.RenodePath = $RenodePath
}

& $script @parameters
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
