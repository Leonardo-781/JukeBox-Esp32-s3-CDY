$clientId = if ($env:SPOTIPY_CLIENT_ID) { $env:SPOTIPY_CLIENT_ID } else { "SEU_CLIENT_ID_AQUI" }
$clientSecret = if ($env:SPOTIPY_CLIENT_SECRET) { $env:SPOTIPY_CLIENT_SECRET } else { "SEU_CLIENT_SECRET_AQUI" }
$redirectUri = "http://127.0.0.1:8888/callback"
$scope = "user-read-playback-state user-modify-playback-state user-read-currently-playing"

# URL de autorização
$authUrl = "https://accounts.spotify.com/authorize?client_id=$clientId&response_type=code&redirect_uri=[System.Web.HttpUtility]::UrlEncode($redirectUri)&scope=[System.Web.HttpUtility]::UrlEncode($scope)"
$authUrl = "https://accounts.spotify.com/authorize?client_id=$clientId&response_type=code&redirect_uri=http%3A%2F%2F127.0.0.1%3A8888%2Fcallback&scope=user-read-playback-state%20user-modify-playback-state%20user-read-currently-playing"

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "   AUTENTICADOR OFICIAL SPOTIFY - VITROLA JUKEBOX" -ForegroundColor Cyan
Write-Host "========================================================`n" -ForegroundColor Cyan

# Inicia listener local na porta 8888
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://127.0.0.1:8888/callback/")
try {
    $listener.Start()
    Write-Host "[1/3] Aguardando clique de autorizacao no navegador..." -ForegroundColor Yellow
    Write-Host "Abrindo link no seu navegador padrao..." -ForegroundColor White
    Start-Process $authUrl

    # Aguarda o retorno do Spotify
    $context = $listener.GetContext()
    $request = $context.Request
    $response = $context.Response

    $code = $request.QueryString["code"]

    if ($code) {
        Write-Host "`n[2/3] Codigo de autorizacao recebido com sucesso!" -ForegroundColor Green
        
        # Envia pagina de confirmacao para o navegador
        $html = "<html><body style='font-family:sans-serif;background:#121212;color:#1ed760;text-align:center;padding:50px;'><h1>Vitrola Jukebox Vinculada com Sucesso!</h1><p style='color:#fff;'>Seu Refresh Token permanente foi gerado. Pode fechar esta janela.</p></body></html>"
        $buffer = [System.Text.Encoding]::UTF8.GetBytes($html)
        $response.ContentLength64 = $buffer.Length
        $response.OutputStream.Write($buffer, 0, $buffer.Length)
        $response.OutputStream.Close()

        # Troca o code pelo Refresh Token permanente
        Write-Host "[3/3] Solicitando Refresh Token permanente ao Spotify..." -ForegroundColor Yellow
        $authHeader = [Convert]::ToBase64String([System.Text.Encoding]::UTF8.GetBytes("${clientId}:${clientSecret}"))
        $headers = @{
            "Authorization" = "Basic $authHeader"
        }
        $body = @{
            grant_type = "authorization_code"
            code = $code
            redirect_uri = $redirectUri
        }

        $tokenResponse = Invoke-RestMethod -Uri "https://accounts.spotify.com/api/token" -Method Post -Headers $headers -Body $body
        
        $refreshToken = $tokenResponse.refresh_token
        Write-Host "`n========================================================" -ForegroundColor Green
        Write-Host " SUCESSO! TOKEN PERMANENTE GERADO:" -ForegroundColor Green
        Write-Host " Refresh Token: $refreshToken" -ForegroundColor White
        Write-Host "========================================================`n" -ForegroundColor Green

        # Salva em arquivo para o servidor e para o firmware
        $tokenData = @{
            client_id = $clientId
            client_secret = $clientSecret
            refresh_token = $refreshToken
            redirect_uri = $redirectUri
        } | ConvertTo-Json

        Set-Content -Path "c:\Users\Leonardo\OneDrive\Documentos\ideias\Jukebox\spotify_token.json" -Value $tokenData -Encoding UTF8
        Write-Host "[OK] Salvo em spotify_token.json com sucesso!" -ForegroundColor Green
    } else {
        Write-Host "[ERRO] Nenhum codigo retornado." -ForegroundColor Red
    }
} catch {
    Write-Host "[ERRO] $_" -ForegroundColor Red
} finally {
    $listener.Stop()
}
