@echo off
setlocal

set ROOT=%~dp0..
cd /d "%ROOT%"

where python >nul 2>nul
if errorlevel 1 (
  echo [wallet] Python non detecte. Lancement installateur...
  powershell -ExecutionPolicy Bypass -File "web\install_wallet_pro.ps1"
)

echo [wallet] Demarrage Addition Wallet Pro...
python "web\addition_wallet_pro.py"

endlocal