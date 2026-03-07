param(
    [string]$RpcHost = "127.0.0.1",
    [int]$Port = 8545,
    [string]$RpcToken = ""
)

function Invoke-RpcLine {
    param([string]$Command)

    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect($RpcHost, $Port)
    $stream = $client.GetStream()
    $writer = New-Object System.IO.StreamWriter($stream)
    $writer.AutoFlush = $true
    $reader = New-Object System.IO.StreamReader($stream)

    $wire = if ([string]::IsNullOrEmpty($RpcToken)) { $Command } else { "$RpcToken $Command" }
    $writer.WriteLine($wire)
    $resp = $reader.ReadLine()

    $reader.Dispose()
    $writer.Dispose()
    $stream.Dispose()
    $client.Dispose()

    return $resp
}

$checks = @(
    @{ name = 'getinfo'; cmd = 'getinfo'; must = 'pq_mode=strict' },
    @{ name = 'monetary_info'; cmd = 'monetary_info'; must = 'max_supply=50000000' },
    @{ name = 'crypto_selftest'; cmd = 'crypto_selftest'; must = 'ok:selftest: ok' }
)

$ok = $true
foreach ($c in $checks) {
    try {
        $r = Invoke-RpcLine -Command $c.cmd
        Write-Host "[$($c.name)] $r"
        if ($r -notlike "*${($c.must)}*") {
            Write-Host "[FAIL] missing expected marker: $($c.must)"
            $ok = $false
        }
    }
    catch {
        Write-Host "[FAIL] $($c.name): $_"
        $ok = $false
    }
}

if (-not $ok) {
    exit 2
}

Write-Host "[OK] healthcheck passed"
exit 0
