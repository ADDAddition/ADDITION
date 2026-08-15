$ErrorActionPreference = 'Stop'

function Invoke-RpcLine {
    param([string]$Command)

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $client.ReceiveTimeout = 5000
        $client.SendTimeout = 5000
        $client.Connect('127.0.0.1', 8545)

        $stream = $client.GetStream()
        $writer = New-Object System.IO.StreamWriter($stream)
        $writer.AutoFlush = $true
        $reader = New-Object System.IO.StreamReader($stream)

        $writer.WriteLine($Command)
        $resp = $reader.ReadLine()

        $reader.Dispose()
        $writer.Dispose()
        $stream.Dispose()
        return $resp
    }
    finally {
        $client.Dispose()
    }
}

$wallet = Invoke-RpcLine 'createwallet'
if (-not $wallet -or $wallet -like 'error*') {
    Write-Host "wallet_error=$wallet"
    exit 1
}

$addrToken = ($wallet -split ' ' | Where-Object { $_ -like 'address=*' } | Select-Object -First 1)
if (-not $addrToken) {
    Write-Host "wallet_parse_error=$wallet"
    exit 1
}

$address = $addrToken.Substring(8)
$mine = Invoke-RpcLine ("mine " + $address)
$balance = Invoke-RpcLine ("getbalance " + $address)
$info = Invoke-RpcLine 'getinfo'

Write-Host ("wallet_address=" + $address)
Write-Host ("mine=" + $mine)
Write-Host ("balance=" + $balance)
Write-Host ("info=" + $info)
