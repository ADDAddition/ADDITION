#Requires -Version 5.1
<#
.SYNOPSIS
  ADDITION desktop first-run helper (Windows).

.DESCRIPTION
  Builds/runs the Flutter desktop wallet and documents how to run additiond.
  additiond is a Linux CMake/liboqs build in this repository — there is no MSVC
  compile path for the node. On Windows, run the node via WSL2 (recommended)
  with scripts/setup_desktop.sh, then point this wallet at 127.0.0.1.

  Modes (loopback write only):
    testnet  -> 127.0.0.1:8545  (default)
    mainnet  -> 127.0.0.1:8546  (local/operator; not a live public network)
    regtest  -> 127.0.0.1:8547  (local min-diff)

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\setup_desktop.ps1 -Check
  powershell -ExecutionPolicy Bypass -File scripts\setup_desktop.ps1 -Mode testnet -RunWallet
#>
param(
  [ValidateSet("testnet", "mainnet", "regtest")]
  [string]$Mode = "testnet",
  [switch]$Check,
  [switch]$RunWallet,
  [switch]$Help
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Root) { $Root = (Get-Location).Path }

function Get-RpcPort([string]$m) {
  switch ($m) {
    "testnet" { return 8545 }
    "mainnet" { return 8546 }
    "regtest" { return 8547 }
  }
}

function Show-Help {
  @'
ADDITION desktop setup (Windows)

Prereqs (install yourself — this script checks, it does not silently invent them):
  1. Git for Windows
  2. Visual Studio 2022 with "Desktop development with C++"
  3. CMake 3.20+ (optional on Windows if you only build Flutter; required in WSL for additiond)
  4. Flutter stable 3.22+  https://docs.flutter.dev/get-started/install/windows
     flutter config --enable-windows-desktop
  5. OpenSSL + liboqs: required to BUILD additiond — do that in WSL2 Ubuntu with:
       wsl -e bash -lc 'cd /path/to/ADDITION && ./scripts/setup_desktop.sh --mode testnet'
  6. Python 3.10+ (optional; used by web/addition_wallet_gui.py)

Modes:
  testnet  write RPC 127.0.0.1:8545
  mainnet  write RPC 127.0.0.1:8546  (banner follows getinfo; height 0 stays 0)
  regtest  write RPC 127.0.0.1:8547

Examples:
  powershell -ExecutionPolicy Bypass -File scripts\setup_desktop.ps1 -Check
  powershell -ExecutionPolicy Bypass -File scripts\setup_desktop.ps1 -Mode testnet -RunWallet

  # Node in WSL (bootstraps operator P2P on testnet by default):
  wsl -e bash -lc "./scripts/setup_desktop.sh --mode testnet --start-only"

Optional loopback tools (after node is up):
  python3 web/evm/evm_rpc_bridge.py
  python3 tools/mining_pool.py coordinator
'@ | Write-Host
}

function Test-Cmd($name) {
  return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

function Test-Rpc([int]$Port) {
  try {
    $client = New-Object System.Net.Sockets.TcpClient
    $iar = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
    $ok = $iar.AsyncWaitHandle.WaitOne(2000, $false)
    if (-not $ok) { $client.Close(); return $false }
    $client.EndConnect($iar)
    $stream = $client.GetStream()
    $bytes = [Text.Encoding]::ASCII.GetBytes("getinfo`n")
    $stream.Write($bytes, 0, $bytes.Length)
    $buf = New-Object byte[] 4096
    $n = $stream.Read($buf, 0, $buf.Length)
    $client.Close()
    $text = [Text.Encoding]::ASCII.GetString($buf, 0, $n)
    return $text -match "network="
  } catch {
    return $false
  }
}

if ($Help) { Show-Help; exit 0 }

$port = Get-RpcPort $Mode
Write-Host "==> Mode=$Mode write=127.0.0.1:$port (loopback only)"

Write-Host "==> Checking prereqs"
$missing = @()
if (-not (Test-Cmd "git")) { $missing += "git" }
if (-not (Test-Cmd "flutter")) { $missing += "flutter (https://docs.flutter.dev/get-started/install/windows)" }
if ($missing.Count -gt 0) {
  Write-Host "Missing:"
  $missing | ForEach-Object { Write-Host "  - $_" }
  Write-Host "Install the missing tools, then re-run -Check."
  if ($Check -or -not $RunWallet) { Show-Help; exit 2 }
}

if (Test-Cmd "flutter") {
  flutter --version | Select-Object -First 1
  flutter config --enable-windows-desktop | Out-Null
}

$rpcOk = Test-Rpc $port
if (-not $rpcOk) {
  if (Test-Cmd "wsl") {
    Write-Host "==> RPC offline — starting node via WSL (one-shot)"
    $repoInWsl = $null
    try { $repoInWsl = (wsl wslpath -a $Root 2>$null | Select-Object -First 1) } catch { }
    if ($repoInWsl) {
      wsl -e bash -lc "cd '$repoInWsl' && ./scripts/setup_desktop.sh --mode $Mode --start-only"
      Start-Sleep -Seconds 2
      $rpcOk = Test-Rpc $port
    }
  }
  if (-not $rpcOk) {
    Write-Host @"
==> RPC offline at 127.0.0.1:$port

One-liner (WSL, from repo root):
  wsl -e bash -lc './scripts/setup_desktop.sh --mode $Mode --start-only'

Then re-run:
  powershell -ExecutionPolicy Bypass -File scripts\setup_desktop.ps1 -Mode $Mode -RunWallet

Write RPC stays 127.0.0.1. Public mine/createwallet stay disabled.
"@
    if ($Check) { exit 1 }
  }
} else {
  Write-Host "==> RPC online at 127.0.0.1:$port"
}

if ($Check) {
  Write-Host "==> Check done"
  exit $(if ($rpcOk) { 0 } else { 1 })
}

if ($RunWallet) {
  Set-Location (Join-Path $Root "client\addition_app")
  flutter pub get
  flutter run -d windows `
    --dart-define=ADDITION_MODE=$Mode `
    --dart-define=ADDITION_RPC_PORT=$port
}

Write-Host @"

Next:
  -Check        verify flutter + RPC
  -RunWallet    launch Flutter Windows wallet
  -Mode regtest|testnet|mainnet

Docs: README.md and client\addition_app\README.md
"@
