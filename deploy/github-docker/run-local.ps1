$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root

Write-Host "[deploy] Starting ADDITION testnet node with Docker Compose"
docker compose -f deploy/github-docker/docker-compose.yml up -d --build

Write-Host "[deploy] Testnet node started (not a live mainnet):"
Write-Host "- Node RPC: 127.0.0.1:8545"
