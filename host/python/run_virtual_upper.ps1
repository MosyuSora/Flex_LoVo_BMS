$ErrorActionPreference = "Stop"

$script = Join-Path $PSScriptRoot "virtual_bms_upper.py"

python $script
exit $LASTEXITCODE
