param([string]$portName = "COM5")
$port = New-Object System.IO.Ports.SerialPort $portName, 115200
$port.DtrEnable = $false
$port.RtsEnable = $false
try {
    $port.Open()
    Write-Host "--- LENDO SERIAL DE $portName (3 segundos) ---"
    for ($i = 0; $i -lt 15; $i++) {
        $chunk = $port.ReadExisting()
        if ($chunk) {
            Write-Host $chunk -NoNewline
        }
        Start-Sleep -Milliseconds 200
    }
    Write-Host ""
    Write-Host "--- FIM DA LEITURA ---"
} catch {
    Write-Host "Erro: $_"
} finally {
    if ($port.IsOpen) { $port.Close() }
}
