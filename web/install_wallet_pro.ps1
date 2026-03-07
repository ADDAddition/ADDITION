$ErrorActionPreference = 'Stop'

Write-Host "[wallet] Addition Wallet Pro installer"

function Test-Command($name) {
    $null -ne (Get-Command $name -ErrorAction SilentlyContinue)
}

if (-not (Test-Command python)) {
    Write-Host "[wallet] Python introuvable. Installation via winget..."
    if (-not (Test-Command winget)) {
        throw "winget introuvable. Installe Winget puis relance ce script."
    }

    winget install --id Python.Python.3.12 -e --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -ne 0) {
        throw "Echec installation Python via winget."
    }

    $pyPath = Join-Path $env:LocalAppData "Programs\Python\Python312"
    if (Test-Path $pyPath) {
        $env:Path = "$pyPath;$pyPath\Scripts;" + $env:Path
    }
}

Write-Host "[wallet] Vérification Python..."
python --version

Write-Host "[wallet] Upgrade pip..."
python -m pip install --upgrade pip

# Dépendances futures (safe même si non utilisées immédiatement)
Write-Host "[wallet] Installation dépendances wallet..."
python -m pip install --upgrade requests

Write-Host "[wallet] Installation terminée."
Write-Host "[wallet] Lance maintenant: python web/addition_wallet_pro.py"