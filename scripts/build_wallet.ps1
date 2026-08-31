# Build Windows mainnet/local wallet executables.
# Run on Windows with Python 3.10+.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Root) { $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path }
$Out = if ($env:ADDITION_WALLET_DIST) { $env:ADDITION_WALLET_DIST } else { Join-Path $Root "web\public\download" }
$Work = Join-Path $Root "packaging\pyi-build"
New-Item -ItemType Directory -Force -Path $Out, $Work | Out-Null

python -c "import PyInstaller" 2>$null
if ($LASTEXITCODE -ne 0) {
  python -m pip install --user "pyinstaller>=6.0"
}

function Build-One([string]$Name, [string]$Src) {
  python -m PyInstaller `
    --noconfirm `
    --clean `
    --onefile `
    --console `
    --name $Name `
    --distpath $Out `
    --workpath (Join-Path $Work $Name) `
    --specpath (Join-Path $Work "spec") `
    $Src
  if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed for $Name" }
}

Build-One "addition-wallet-mainnet" (Join-Path $Root "web\addition_wallet_gui.py")
Build-One "addition-wallet-cli-mainnet" (Join-Path $Root "web\addition_wallet.py")

$Gui = Join-Path $Out "addition-wallet-mainnet.exe"
$Cli = Join-Path $Out "addition-wallet-cli-mainnet.exe"
python (Join-Path $Root "scripts\smoke_wallet_binary.py") $Gui $Cli
Write-Host "wallet binaries ready in $Out"
