param([string]$portName = "COM5")
$port = New-Object System.IO.Ports.SerialPort $portName, 115200
$port.DtrEnable = $false
$port.RtsEnable = $false
try {
    $port.Open()
    $port.RtsEnable = $true
    Start-Sleep -Milliseconds 150
    $port.RtsEnable = $false
    Start-Sleep -Milliseconds 100
    
    Write-Host "--- BOOT LOG DE $portName (4 segundos) ---"
    for ($i = 0; $i -lt 20; $i++) {
        $chunk = $port.ReadExisting()
        if ($chunk) {
            Write-Host $chunk -NoNewline
        }
        Start-Sleep -Milliseconds 200
    }
    Write-Host ""
    Write-Host "--- FIM DO LOG ---"
} catch {
    Write-Host "Erro: $_"
} finally {
    if ($port.IsOpen) { $port.Close() }
}
