$port = New-Object System.IO.Ports.SerialPort 'COM25', 115200
$port.DtrEnable = $false
$port.RtsEnable = $false
try {
    $port.Open()
    Write-Host "Monitorando COM25 por 15 segundos..."
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline) {
        if ($port.BytesToRead -gt 0) {
            $data = $port.ReadExisting()
            Write-Host -NoNewline $data
        }
        Start-Sleep -Milliseconds 100
    }
} catch {
    Write-Host "Erro: $_"
} finally {
    if ($port.IsOpen) { $port.Close() }
}
