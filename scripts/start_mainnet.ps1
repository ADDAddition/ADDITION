$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Assert-Ok([string]$step) {
    if ($LASTEXITCODE -ne 0) {
        throw "[mainnet] $step failed with exit code $LASTEXITCODE"
    }
}

# Separate chain. Do not reuse the testnet data/ directory.
$dataDir = Join-Path $root "data-mainnet"
if (-not (Test-Path $dataDir)) {
    New-Item -ItemType Directory -Path $dataDir -Force | Out-Null
}

Get-Process additiond -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

Write-Host "[mainnet] building..."
cmake -S . -B build
Assert-Ok "cmake configure"
cmake --build build
Assert-Ok "cmake build"

$exe = Join-Path $root "build\additiond.exe"
if (-not (Test-Path $exe)) {
    throw "additiond.exe not found"
}

if (-not $env:ADDITION_PRIVACY_MASTER_KEY -or $env:ADDITION_PRIVACY_MASTER_KEY.Length -lt 32) {
    throw "ADDITION_PRIVACY_MASTER_KEY must be set (min 32 chars) for --mainnet"
}
if (-not $env:ADDITION_RPC_TOKEN) {
    Write-Host "[mainnet][WARN] ADDITION_RPC_TOKEN not set (local RPC auth disabled)."
}
if ($env:ADDITION_ENABLE_LAN_RPC -eq "1" -and -not $env:ADDITION_LAN_RPC_TOKEN) {
    Write-Host "[mainnet][WARN] LAN RPC enabled but ADDITION_LAN_RPC_TOKEN is empty."
}
if (-not $env:ADDITION_STRICT_ADMIN_MODE) {
    $env:ADDITION_STRICT_ADMIN_MODE = "1"
    Write-Host "[mainnet] ADDITION_STRICT_ADMIN_MODE defaulted to 1"
}
if (-not $env:ADDITION_ALLOW_INSECURE_TX_COMMANDS) {
    $env:ADDITION_ALLOW_INSECURE_TX_COMMANDS = "0"
    Write-Host "[mainnet] ADDITION_ALLOW_INSECURE_TX_COMMANDS defaulted to 0"
}

Write-Host "[mainnet] starting public ADDITION_MAINNET_V1 (seed 34.27.30.115:28546 / 38546)..."
& $exe --mainnet --config config-mainnet.toml --genesis genesis-mainnet.json --data-dir $dataDir
