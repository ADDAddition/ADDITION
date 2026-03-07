$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Test-Command($name) {
    $null -ne (Get-Command $name -ErrorAction SilentlyContinue)
}

if (-not (Test-Command python)) {
    Write-Host "[wallet] Python non detecte. Installation automatique..."
    & "$PSScriptRoot\install_wallet_pro.ps1"
}

Write-Host "[wallet] Demarrage Addition Wallet Pro"
python "$PSScriptRoot\addition_wallet_pro.py"