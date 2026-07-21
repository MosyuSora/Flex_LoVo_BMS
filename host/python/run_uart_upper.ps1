param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$script = Join-Path $PSScriptRoot "uart_upper.py"
$arguments = @($script, "--baud", $Baud)
if (-not [string]::IsNullOrWhiteSpace($Port)) {
    $arguments += @("--port", $Port)
}
& $Python @arguments
exit $LASTEXITCODE
