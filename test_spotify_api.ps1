$cfg = Get-Content 'c:\Users\Leonardo\OneDrive\Documentos\ideias\Jukebox\spotify_token.json' | ConvertFrom-Json
$b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("$($cfg.client_id):$($cfg.client_secret)"))
$res = Invoke-RestMethod -Uri 'https://accounts.spotify.com/api/token' -Method Post -Headers @{Authorization="Basic $b64"} -Body @{grant_type='refresh_token'; refresh_token=$cfg.refresh_token}

Write-Host ">>> SUCESSO: Conexao com API do Spotify validada!" -ForegroundColor Green
$headers = @{Authorization="Bearer $($res.access_token)"}
$devices = Invoke-RestMethod -Uri 'https://api.spotify.com/v1/me/player/devices' -Method Get -Headers $headers
Write-Host ">>> Aparelhos Spotify detectados na sua conta:" -ForegroundColor Cyan
if ($devices.devices.Count -eq 0) {
    Write-Host "Nenhum aparelho ativo no momento (basta abrir o Spotify no celular/PC/JBL)" -ForegroundColor Yellow
} else {
    $devices.devices | Select-Object name, type, is_active | Format-Table -AutoSize
}
