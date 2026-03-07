$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$exe = Join-Path $root "build\additiond.exe"
if (-not (Test-Path $exe)) {
    throw "additiond.exe introuvable. Compile d'abord avec: cmake -S . -B build ; cmake --build build"
}

Write-Host "[bootstrap] activation RPC reseau: LAN=1 P2P=1"
$env:ADDITION_ENABLE_LAN_RPC = "1"
$env:ADDITION_ENABLE_P2P_RPC = "1"

Write-Host "[bootstrap] demarrage du noeud public..."
& $exe
