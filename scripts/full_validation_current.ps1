param(
    [string]$RpcHost = "127.0.0.1",
    [int]$Port = 8545
)

$ErrorActionPreference = 'Stop'

function Invoke-RpcLine {
    param([string]$Command)

    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect($RpcHost, $Port)
    $stream = $client.GetStream()
    $writer = New-Object System.IO.StreamWriter($stream)
    $writer.AutoFlush = $true
    $reader = New-Object System.IO.StreamReader($stream)

    $writer.WriteLine($Command)
    $resp = $reader.ReadLine()

    $reader.Dispose()
    $writer.Dispose()
    $stream.Dispose()
    $client.Dispose()

    return $resp
}

function Expect-Contains {
    param(
        [string]$Name,
        [string]$Response,
        [string]$Must
    )
    if ([string]::IsNullOrEmpty($Response) -or -not $Response.Contains($Must)) {
        throw "[$Name] expected '$Must' but got: $Response"
    }
}

Write-Host "[1] getinfo"
$getinfo = Invoke-RpcLine "getinfo"
Expect-Contains -Name "getinfo" -Response $getinfo -Must "pq_mode=strict"
Expect-Contains -Name "getinfo" -Response $getinfo -Must "privacy_mode=sha3_opening"
Write-Host $getinfo

Write-Host "[2] use static miner address"
$addr = "miner1"
Write-Host ("address={0}" -f $addr)

Write-Host "[3] mine"
$mine = Invoke-RpcLine ("mine {0}" -f $addr)
Expect-Contains -Name "mine" -Response $mine -Must "mined block"
Write-Host $mine

Write-Host "[4] getbalance"
$bal = Invoke-RpcLine ("getbalance {0}" -f $addr)
if (-not [uint64]::TryParse($bal, [ref]([uint64]0))) {
    throw "[getbalance] invalid numeric response: $bal"
}
Write-Host ("balance={0}" -f $bal)

Write-Host "[5] benchmark_lane_fill + benchmark_lanes"
$fill = Invoke-RpcLine "benchmark_lane_fill 200 privacy"
if ($fill -ne "ok") { throw "[benchmark_lane_fill] unexpected: $fill" }
$lanes = Invoke-RpcLine "benchmark_lanes"
Expect-Contains -Name "benchmark_lanes" -Response $lanes -Must "lane_privacy="
Write-Host $lanes

Write-Host "[6] benchmark_mine"
$bench = Invoke-RpcLine "benchmark_mine 1"
Expect-Contains -Name "benchmark_mine" -Response $bench -Must "bench_avg_tps="
Write-Host $bench

$tpsText = [regex]::Match($bench, 'bench_avg_tps=([0-9]+(\.[0-9]+)?)').Groups[1].Value
$benchTps = 0.0
if (-not [double]::TryParse($tpsText, [ref]$benchTps)) {
    throw "[benchmark_mine] cannot parse TPS from: $bench"
}

Write-Host "[7] privacy_status"
$pstatus = Invoke-RpcLine "privacy_status"
Expect-Contains -Name "privacy_status" -Response $pstatus -Must "notes="
Write-Host $pstatus

Write-Host "[8] protocol_status"
$proto = Invoke-RpcLine "protocol_status"
Expect-Contains -Name "protocol_status" -Response $proto -Must "finality="
Write-Host $proto

$privacyOk = ($getinfo.Contains("privacy_mode=sha3_opening") -and $getinfo.Contains("privacy_ok=true"))
$tpsObjective = 100000.0
$tpsOk = ($benchTps -ge $tpsObjective)

Write-Host "===== FINAL OBJECTIVE CHECK ====="
Write-Host ("privacy_enabled={0}" -f $privacyOk)
Write-Host ("bench_tps={0}" -f $benchTps)
Write-Host ("target_tps={0}" -f $tpsObjective)
Write-Host ("tps_ok={0}" -f $tpsOk)
Write-Host ("objective_100_ok={0}" -f ($privacyOk -and $tpsOk))

if (-not ($privacyOk -and $tpsOk)) {
    exit 2
}

exit 0
