param(
    [string]$RpcHost = "127.0.0.1",
    [int]$Port = 8545,
    [string]$RpcToken = "",
    [int]$BenchBlocks = 2,
    [int]$BenchTxPerBlock = 2000
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

    $wire = if ([string]::IsNullOrEmpty($RpcToken)) { $Command } else { "$RpcToken $Command" }
    $writer.WriteLine($wire)
    $resp = $reader.ReadLine()

    $reader.Dispose()
    $writer.Dispose()
    $stream.Dispose()
    $client.Dispose()

    return $resp
}

$script:results = @()

function Add-Result {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$Response,
        [string]$Expectation
    )
    $script:results += [PSCustomObject]@{
        test = $Name
        ok = $Ok
        expected = $Expectation
        response = $Response
    }
}

function Test-Contains {
    param(
        [string]$Value,
        [string]$Must
    )
    if ([string]::IsNullOrEmpty($Value) -or [string]::IsNullOrEmpty($Must)) {
        return $false
    }
    return $Value.Contains($Must)
}

$baseChecks = @(
    @{ name = 'getinfo'; cmd = 'getinfo'; must = 'pq_mode=strict' },
    @{ name = 'monetary_info'; cmd = 'monetary_info'; must = 'max_supply=50000000' },
    @{ name = 'crypto_selftest'; cmd = 'crypto_selftest'; must = 'ok:' },
    @{ name = 'createwallet'; cmd = 'createwallet'; must = 'address=' }
)

foreach ($c in $baseChecks) {
    try {
        $r = Invoke-RpcLine -Command $c.cmd
        $ok = Test-Contains -Value $r -Must $c.must
        Add-Result -Name $c.name -Ok $ok -Response $r -Expectation $c.must
    } catch {
        Add-Result -Name $c.name -Ok $false -Response $_.Exception.Message -Expectation $c.must
    }
}

try {
    $r = Invoke-RpcLine -Command "privacy_set_verifier tools/zk_verify_wrapper.py"
    Add-Result -Name "privacy_set_verifier" -Ok ($r -like "ok*") -Response $r -Expectation "ok"
} catch {
    Add-Result -Name "privacy_set_verifier" -Ok $false -Response $_.Exception.Message -Expectation "ok"
}

try {
    $r = Invoke-RpcLine -Command "privacy_strict_mode on"
    Add-Result -Name "privacy_strict_mode" -Ok ($r -eq "ok") -Response $r -Expectation "ok"
} catch {
    Add-Result -Name "privacy_strict_mode" -Ok $false -Response $_.Exception.Message -Expectation "ok"
}

try {
    $r = Invoke-RpcLine -Command "privacy_status"
    $ok = (Test-Contains -Value $r -Must "strict_zk_mode=true") -and (Test-Contains -Value $r -Must "verifier_configured=true")
    Add-Result -Name "privacy_status" -Ok $ok -Response $r -Expectation "strict_zk_mode=true & verifier_configured=true"
} catch {
    Add-Result -Name "privacy_status" -Ok $false -Response $_.Exception.Message -Expectation "strict_zk_mode=true & verifier_configured=true"
}

try {
    $r = Invoke-RpcLine -Command ("benchmark_objective {0} {1}" -f $BenchBlocks, $BenchTxPerBlock)
    $ok = Test-Contains -Value $r -Must "objective_tps_ok=true"
    Add-Result -Name "benchmark_objective" -Ok $ok -Response $r -Expectation "objective_tps_ok=true"
} catch {
    Add-Result -Name "benchmark_objective" -Ok $false -Response $_.Exception.Message -Expectation "objective_tps_ok=true"
}

try {
    $r = Invoke-RpcLine -Command "protocol_status"
    $ok = Test-Contains -Value $r -Must "objective_100_ok=true"
    Add-Result -Name "protocol_status" -Ok $ok -Response $r -Expectation "objective_100_ok=true"
} catch {
    Add-Result -Name "protocol_status" -Ok $false -Response $_.Exception.Message -Expectation "objective_100_ok=true"
}

$passCount = ($script:results | Where-Object { $_.ok }).Count
$totalCount = $script:results.Count

Write-Host "===== OBJECTIVE TEST REPORT ====="
$script:results | ForEach-Object {
    $status = if ($_.ok) { "PASS" } else { "FAIL" }
    Write-Host ("[{0}] {1}" -f $status, $_.test)
    Write-Host ("  expected: {0}" -f $_.expected)
    Write-Host ("  response: {0}" -f $_.response)
}
Write-Host ("SUMMARY: {0}/{1} tests passed" -f $passCount, $totalCount)

if ($passCount -ne $totalCount) {
    exit 2
}

exit 0
