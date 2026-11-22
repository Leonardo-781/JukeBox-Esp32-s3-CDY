#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>
#include <math.h>

// Protecao contra brownout em picos de corrente
extern "C" void esp_brownout_disable(void);

// Desativa detector de brownout no carregamento do binario
struct EarlyInit {
  EarlyInit() {
    esp_brownout_disable();
  }
};
static EarlyInit s_early_init;

#include "config.h"

// ==============================================================================
// CONFIGURACOES DA CAPA GIRATORIA (SPOTIFY VINYL LABEL)
// ==============================================================================
#define COVER_FRAME_SIZE      56
#define COVER_FRAMES_COUNT    8
#define COVER_TOTAL_BYTES     (COVER_FRAMES_COUNT * COVER_FRAME_SIZE * COVER_FRAME_SIZE * 2)

uint8_t* coverFramesBuffer    = nullptr;
bool hasCoverFrames           = false;
String lastCoverTrack         = "";

// Vigia da tela: detecta se o ILI9341 perdeu a configuracao (queda de energia/ruido)
uint8_t tftModeAtBoot         = 0;
bool tftWatchdogEnabled       = false;
uint32_t tftRecoveries        = 0;
unsigned long lastTftCheckMs  = 0;
void checkDisplayHealth();

// ==============================================================================
// INSTANCIAS DE HARDWARE
// ==============================================================================
Adafruit_ILI9341 tft = Adafruit_ILI9341(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
MFRC522 rfid(PIN_RFID_SDA, PIN_RFID_RST);
Preferences prefs;
WebServer portalServer(80);
DNSServer dnsServer;
I2SClass i2s;
WiFiClient audioClient;

// ==============================================================================
// BUFFER CIRCULAR E VARIAVEIS DE AUDIO I2S (STREAM TCP 44.1 kHz 16-BIT MONO)
// ==============================================================================
#define AUDIO_RING_MASK       (AUDIO_RING_SIZE - 1)
alignas(4) static uint8_t audioRing[AUDIO_RING_SIZE];
static volatile uint32_t audioRingHead = 0;
static volatile uint32_t audioRingTail = 0;
static volatile bool audioBuffering    = true;
static volatile uint32_t audioUnderruns = 0;
static volatile bool toneRequest       = false;
static unsigned long lastAudioReconnect = 0;
static unsigned long lastAudioStatsMs  = 0;
static uint32_t audioBytesRecebidos    = 0;

// ==============================================================================
// VARIAVEIS DE ESTADO DO SISTEMA
// ==============================================================================
bool isWifiConnected     = false;
bool isCaptivePortalOpen = false;
bool isDiscPresent       = false;
bool isPlaying           = false;
bool isDiscRegistered    = false;
bool useCloudServer      = false;

String cloudServerUrl    = DEFAULT_CLOUD_URL;
String scannedWifiHtml   = "";

char activeUidHex[16]    = "NENHUM";
String currentTrackTitle = "Aproxime um vinil";
String currentArtist     = "Vitrola Jukebox";
String currentDiscName   = "Prato livre";

int progressSeconds      = 0;
int durationSeconds      = 180;
int currentVolume        = 50;
int currentMotorSpeed    = MOTOR_DEFAULT_SPEED;

uint8_t missingChecksCount  = 0;
uint8_t vinylAnimStep       = 0;

unsigned long lastRfidCheckTime    = 0;
unsigned long lastAnimTickTime     = 0;
unsigned long lastSecondTick       = 0;
unsigned long lastServerSyncTime   = 0;
unsigned long lastVolumePopupTime  = 0;
bool showVolumePopup               = false;

// Estado do Encoder Rotativo (Volume, Play/Pause e Segurar 3s -> Portal Wi-Fi)
int lastClkState                   = HIGH;
unsigned long lastEncoderBtnPress  = 0;
unsigned long encBtnDownTime       = 0;
bool encBtnWasDown                 = false;
bool portalTriggeredByHold         = false;

// ==============================================================================
// PROTOTIPOS DE FUNCOES
// ==============================================================================
void setupHardware();
bool tryConnectWifi(const String& ssid, const String& pass, uint32_t timeoutMs);
void detectBestServer();
void setupWiFiConnection();
void startCaptivePortal();
void drawFullUI();
void drawHeader();
void drawVinylVisualizer(uint8_t step);
void drawTrackInfo();
void drawProgressBar();
void drawVolumePopup();
void handleEncoder();
void startMotorWithKick(int speed);
void stopMotor();
bool checkCardStillPresent(const char* targetUid);
void handleDiscPlaced(const char* uidHex);
void handleDiscRemoved();
String buildApiUrl(const String& path);
void beginHttpClient(HTTPClient& http, WiFiClient& plainClient, WiFiClientSecure& secureClient, const String& url, int timeoutMs);
bool performHttpGet(const String& path, String& payloadOut, int timeoutMs = 3000);
bool sendServerPost(const String& path, const String& jsonPayload, String& responseOut);
void syncServerStatus();
bool fetchCoverFrames();
void reinitRfidRegisters(MFRC522::PCD_RxGain gain = MFRC522::RxGain_max);
void audioTask(void*);
void audioNetTask(void*);
void connectAudioStream();
void pumpNetworkAudio();
void playToneMono(float freqHz, int durationMs, float amp = 0.25f);
void playStartupChime();

// ==============================================================================
// SETUP PRINCIPAL
// ==============================================================================
void setup() {
  esp_brownout_disable();
  Serial.begin(115200);
  delay(300);

  Serial.println("\n========================================================");
  Serial.println("   VITROLA JUKEBOX OFICIAL - ESP32 38P + SERVIDOR STI");
  Serial.println("========================================================");
  Serial.printf("[SISTEMA] Motivo do ultimo boot: %d\n", esp_reset_reason());

  // 1. Inicializacao de I/O basico
  setupHardware();

  // 2. Apresentacao no Display
  digitalWrite(PIN_RFID_SDA, HIGH);
  tft.fillScreen(COL_BG);
  tft.setCursor(30, 70);
  tft.setTextColor(COL_WHITE);
  tft.setTextSize(3);
  tft.print("VITROLA 3D");

  tft.setCursor(30, 110);
  tft.setTextColor(COL_SPOTIFY);
  tft.setTextSize(2);
  tft.print("Iniciando Sistema...");

  // 3. Conexao Wi-Fi
  setupWiFiConnection();
  Serial.println("[DIAGNOSTICO] >>> PASSO 1: Wi-Fi CONECTADO COM SUCESSO <<<");

  // 4. Som de Boas-Vindas Suave
  playStartupChime();
  Serial.println("[DIAGNOSTICO] >>> PASSO 2: SOM DE BOAS-VINDAS CONCLUIDO <<<");

  // 5. Garante que o display esteja inicializado e desenhado apos o som
  delay(100);
  digitalWrite(PIN_RFID_SDA, HIGH);
  tft.begin(20000000);
  tft.setRotation(TFT_ROTATION);
  if (!isCaptivePortalOpen) {
    drawFullUI();
  }
  Serial.println("[DIAGNOSTICO] >>> PASSO 3: UI COMPLETA REDESENHADA NA TELA <<<");

  // 6. Tasks de Audio I2S em Segundo Plano
  xTaskCreatePinnedToCore(audioTask, "audioI2S", 4096, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(audioNetTask, "audioNet", 4096, nullptr, 2, nullptr, 0);
  Serial.println("[DIAGNOSTICO] >>> PASSO 4: TASKS DE AUDIO INICIADAS <<<");

  Serial.println("[SISTEMA] Vitrola pronta! Aproxime um mini disco de vinil 3D.");
}

// ==============================================================================
// LOOP PRINCIPAL (NAO-BLOQUEANTE)
// ==============================================================================
void loop() {
  unsigned long now = millis();

  // 1. Processa Portal Wi-Fi se estiver em modo AP
  if (isCaptivePortalOpen) {
    dnsServer.processNextRequest();
    portalServer.handleClient();
    return;
  }

  // 2. Leitura do Encoder Rotativo (Volume e Play/Pause)
  handleEncoder();

  // 3. Verificacao do Leitor RFID no Prato (Inicia apenas apos 10s de boot para isolar teste de tela)
  static bool rfidStartedLog = false;
  if (!rfidStartedLog && now > 10000) {
    rfidStartedLog = true;
    Serial.println("[DIAGNOSTICO] >>> PASSO 5: INICIANDO CHECAGEM PERIODICA RFID AGORA <<<");
  }

  if (now > 10000 && (now - lastRfidCheckTime >= RFID_CHECK_INTERVAL)) {
    lastRfidCheckTime = now;

    if (isDiscPresent) {
      bool present = checkCardStillPresent(activeUidHex);
      if (present) {
        missingChecksCount = 0;
      } else {
        missingChecksCount++;
        Serial.printf("[RFID] Falha %d/%d no prato girando\n", missingChecksCount, MAX_DISC_MISSES);
        if (missingChecksCount >= MAX_DISC_MISSES) {
          Serial.println("[RFID] Disco retirado da vitrola!");
          handleDiscRemoved();
        }
      }
    } else {
      digitalWrite(PIN_TFT_CS, HIGH);

      byte bufferATQA[2];
      byte bufferSize = sizeof(bufferATQA);
      bool tagDetected = false;

      // Tentativa 1: Ganho Maximo (48dB - ideal para 5mm a 25mm)
      reinitRfidRegisters(rfid.RxGain_max);
      if (rfid.PICC_WakeupA(bufferATQA, &bufferSize) == MFRC522::STATUS_OK) {
        tagDetected = true;
      } else {
        bufferSize = sizeof(bufferATQA);
        if (rfid.PICC_RequestA(bufferATQA, &bufferSize) == MFRC522::STATUS_OK) {
          tagDetected = true;
        }
      }

      // Tentativa 2: Ganho Medio (33dB - evita saturacao do receptor a 0mm)
      if (!tagDetected) {
        reinitRfidRegisters(rfid.RxGain_33dB);
        bufferSize = sizeof(bufferATQA);
        if (rfid.PICC_WakeupA(bufferATQA, &bufferSize) == MFRC522::STATUS_OK) {
          tagDetected = true;
        } else {
          bufferSize = sizeof(bufferATQA);
          if (rfid.PICC_RequestA(bufferATQA, &bufferSize) == MFRC522::STATUS_OK) {
            tagDetected = true;
          }
        }
      }

      if (tagDetected) {
        if (rfid.PICC_ReadCardSerial()) {
          char uidBuffer[16];
          snprintf(uidBuffer, sizeof(uidBuffer), "%02X%02X%02X%02X",
                   rfid.uid.uidByte[0], rfid.uid.uidByte[1],
                   rfid.uid.uidByte[2], rfid.uid.uidByte[3]);
          rfid.PICC_HaltA();
          rfid.PCD_StopCrypto1();

          handleDiscPlaced(uidBuffer);
        }
      }
      digitalWrite(PIN_RFID_SDA, HIGH);
    }
  }

  // 4. Animacao do Vinil / Capa Giratoria na Tela (Rotacao Lenta Suave)
  if (now - lastAnimTickTime >= VINYL_ANIM_INTERVAL_MS) {
    lastAnimTickTime = now;
    if (isDiscPresent && isPlaying) {
      vinylAnimStep = (vinylAnimStep + 1) % COVER_FRAMES_COUNT;
      digitalWrite(PIN_RFID_SDA, HIGH);
      drawVinylVisualizer(vinylAnimStep);
    }
  }

  // 5. Avanco do Timer de Musica Segundo a Segundo (Local Fluido)
  if (now - lastSecondTick >= 1000) {
    lastSecondTick = now;
    if (isDiscPresent && isPlaying) {
      if (progressSeconds < durationSeconds) {
        progressSeconds++;
        digitalWrite(PIN_RFID_SDA, HIGH);
        drawProgressBar();
      }
    }
  }

  // 6. Sincronizacao Periodica com Servidor STI (Ajusta Relogio e Capa se mudar)
  if (isWifiConnected && (now - lastServerSyncTime >= 3500)) {
    lastServerSyncTime = now;
    if (isDiscPresent && isPlaying) {
      syncServerStatus();
    }
  }

  // 7. Oculta popup de volume apos 1.5s
  if (showVolumePopup && (now - lastVolumePopupTime > 1500)) {
    showVolumePopup = false;
    digitalWrite(PIN_RFID_SDA, HIGH);
    drawProgressBar();
  }

  // 8. Diagnostico periodico do stream de audio I2S
  if (now - lastAudioStatsMs >= 5000) {
    lastAudioStatsMs = now;
    if (audioBytesRecebidos > 0 || (!audioBuffering && isPlaying)) {
      Serial.printf("[STREAM I2S] %lu KB/s | buffer %lu ms | %s | falhas %lu | vol %d%%\n",
                    (unsigned long)(audioBytesRecebidos / 5120),
                    (unsigned long)(audioRingUsed() * 1000UL / (SAMPLE_RATE * 2)),
                    isPlaying ? "tocando" : "pausado",
                    (unsigned long)audioUnderruns, currentVolume);
    }
    audioBytesRecebidos = 0;
  }

  // 9. Vigia da tela (a cada 2 s)
  if (now - lastTftCheckMs >= 2000) {
    lastTftCheckMs = now;
    checkDisplayHealth();
  }
}

void checkDisplayHealth() {
  digitalWrite(PIN_RFID_SDA, HIGH);
  uint8_t mode = tft.readcommand8(ILI9341_RDMODE);
  if (!tftWatchdogEnabled) return;
  if (mode == 0x9C) return;

  tftRecoveries++;
  Serial.printf("[TFT] ALERTA: tela perdeu a configuracao (RDMODE=0x%02X) | WiFi RSSI %d | motor %s | recuperacao #%lu\n",
                mode, WiFi.RSSI(), isPlaying ? "LIGADO" : "desligado", (unsigned long)tftRecoveries);
  tft.begin(20000000);
  tft.setRotation(TFT_ROTATION);
  drawFullUI();
}

// ==============================================================================
// INICIALIZACAO DE HARDWARE
// ==============================================================================
void setupHardware() {
  pinMode(PIN_MOTOR_IN1, OUTPUT);
  pinMode(PIN_MOTOR_IN2, OUTPUT);
  stopMotor();

  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);
  lastClkState = digitalRead(PIN_ENC_CLK);

  // 1. Reset fisico da Tela TFT (identico ao teste validado)
  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(20);
  digitalWrite(PIN_TFT_RST, LOW);
  delay(50);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(100);

  // 2. Garante deselecao do RFID antes do SPI
  pinMode(PIN_RFID_SDA, OUTPUT);
  digitalWrite(PIN_RFID_SDA, HIGH);
  pinMode(PIN_RFID_RST, OUTPUT);
  digitalWrite(PIN_RFID_RST, HIGH);

  // 3. Inicializa Barramento SPI de Hardware
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  // 4. Inicializacao do Display ILI9341 (Adafruit cuida do CS)
  Serial.println("[TFT] Inicializando ILI9341...");
  tft.begin(20000000);
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(COL_BG);
  tftModeAtBoot = tft.readcommand8(ILI9341_RDMODE);
  tftWatchdogEnabled = (tftModeAtBoot == 0x9C);
  Serial.printf("[TFT] Display OK! RDMODE=0x%02X -> vigia da tela %s\n", tftModeAtBoot,
                tftWatchdogEnabled ? "ATIVO" : "INDISPONIVEL (MISO da tela nao responde)");

  // 5. Inicializacao do Leitor RFID RC522
  Serial.println("[RFID] Inicializando RC522...");
  rfid.PCD_Init();
  delay(20);
  reinitRfidRegisters(rfid.RxGain_max);
  digitalWrite(PIN_RFID_SDA, HIGH);

  // Inicializacao do I2S MAX98357A
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("[ERRO] Falha ao inicializar I2S MAX98357A!");
  } else {
    Serial.printf("[OK] I2S MAX98357A ativado (BCLK:%d, LRC:%d, DIN:%d)\n", PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN);
  }
}

// ==============================================================================
// CONEXAO WI-FI INTELIGENTE (DUAL WI-FI + AUTO DETECCAO LAN/CLOUD + PORTAL)
// ==============================================================================
bool tryConnectWifi(const String& ssid, const String& pass, uint32_t timeoutMs) {
  if (ssid.length() == 0) return false;

  digitalWrite(PIN_RFID_SDA, HIGH);
  tft.fillRect(20, 140, 280, 45, COL_BG);
  tft.setCursor(20, 150);
  tft.setTextColor(COL_AMBER);
  tft.setTextSize(1);
  tft.printf("Conectando Wi-Fi: %s...", ssid.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  return (WiFi.status() == WL_CONNECTED);
}

void detectBestServer() {
  useCloudServer = false;
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  WiFiClient plainClient;
  String lanProbe = "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) + "/api/status";
  http.begin(plainClient, lanProbe);
  http.setTimeout(4000);
  int code = http.GET();
  http.end();

  if (code == 200) {
    useCloudServer = false;
    Serial.printf("[REDE] Modo LAN confirmado (Servidor STI local %s:%d)\n", SERVER_HOST, SERVER_PORT);
  } else {
    useCloudServer = false;
    Serial.printf("[REDE] Servidor STI local inicializando (status %d) -> Mantendo LAN %s:%d\n", code, SERVER_HOST, SERVER_PORT);
  }
}

void setupWiFiConnection() {
  prefs.begin("jukebox-wifi", true);
  String savedSSID  = prefs.getString("ssid", DEFAULT_WIFI_SSID);
  String savedPASS  = prefs.getString("pass", DEFAULT_WIFI_PASS);
  cloudServerUrl    = prefs.getString("cloud_url", DEFAULT_CLOUD_URL);
  prefs.end();

  cloudServerUrl.trim();
  while (cloudServerUrl.endsWith("/")) {
    cloudServerUrl.remove(cloudServerUrl.length() - 1);
  }
  if (cloudServerUrl.length() == 0) cloudServerUrl = DEFAULT_CLOUD_URL;

  bool connected = tryConnectWifi(savedSSID, savedPASS, 7000);
  if (!connected && savedSSID != String(DEFAULT_WIFI_SSID)) {
    Serial.println("\n[WIFI] Rede salva indisponivel, tentando rede de casa...");
    connected = tryConnectWifi(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS, 7000);
  }

  if (connected) {
    isWifiConnected = true;
    Serial.printf("\n[WIFI] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
    detectBestServer();

    digitalWrite(PIN_RFID_SDA, HIGH);
    tft.setCursor(20, 170);
    tft.setTextColor(COL_SPOTIFY);
    tft.printf("IP: %s | %s", WiFi.localIP().toString().c_str(), useCloudServer ? "CLOUD" : "LAN");
    delay(800);
    return;
  }

  Serial.println("\n[WIFI] Abrindo Portal Captivo...");
  startCaptivePortal();
}

void startCaptivePortal() {
  isCaptivePortalOpen = true;
  isWifiConnected = false;
  isPlaying = false;
  stopMotor();

  digitalWrite(PIN_RFID_SDA, HIGH);
  tft.fillScreen(COL_BG);
  tft.setCursor(20, 80);
  tft.setTextColor(COL_AMBER);
  tft.setTextSize(2);
  tft.print("ESCANEANDO WI-FI...");

  // Escaneia redes antes de subir o Access Point
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  int n = WiFi.scanNetworks();

  scannedWifiHtml = "";
  int maxShow = min(n, 8);
  for (int i = 0; i < maxShow; i++) {
    String netSsid = WiFi.SSID(i);
    if (netSsid.length() == 0) continue;
    String safeSsid = netSsid;
    safeSsid.replace("&", "&amp;");
    safeSsid.replace("<", "&lt;");
    safeSsid.replace(">", "&gt;");
    safeSsid.replace("\"", "&quot;");
    safeSsid.replace("'", "&#39;");
    String jsSsid = netSsid;
    jsSsid.replace("\\", "\\\\");
    jsSsid.replace("'", "\\'");
    scannedWifiHtml += "<button type='button' class='net' onclick=\"document.getElementById('ssidIn').value='" + jsSsid + "';\">";
    scannedWifiHtml += safeSsid + " (" + String(WiFi.RSSI(i)) + " dBm)</button>";
  }
  WiFi.scanDelete();

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Vitrola-Jukebox", "");
  delay(100);

  dnsServer.start(53, "*", WiFi.softAPIP());

  static bool portalRoutesReady = false;
  if (!portalRoutesReady) {
    portalRoutesReady = true;

    portalServer.on("/", HTTP_GET, []() {
      String safeCloud = cloudServerUrl;
      safeCloud.replace("'", "&#39;");
      safeCloud.replace("\"", "&quot;");
      String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Vitrola Wi-Fi</title>";
      html += "<style>body{background:#121212;color:#fff;font-family:sans-serif;padding:20px;max-width:420px;margin:auto;}";
      html += "h2{color:#1db954;text-align:center;}";
      html += "input{width:100%;padding:11px;margin:7px 0;box-sizing:border-box;border-radius:6px;border:none;font-size:15px;}";
      html += "button{background:#1db954;color:#000;font-weight:bold;padding:12px 16px;border:none;border-radius:6px;width:100%;margin-top:10px;font-size:15px;cursor:pointer;}";
      html += "button.net{background:#222;color:#fff;font-weight:normal;text-align:left;margin-top:6px;border:1px solid #333;}</style></head><body>";
      html += "<h2>Vitrola Jukebox</h2><p>Escolha a rede Wi-Fi do local (ou roteador do celular):</p>";
      if (scannedWifiHtml.length() > 0) {
        html += scannedWifiHtml;
      }
      html += "<form method='POST' action='/save' style='margin-top:14px;'>";
      html += "<input id='ssidIn' name='ssid' placeholder='Nome da Rede Wi-Fi (SSID)' required>";
      html += "<input name='pass' type='password' placeholder='Senha do Wi-Fi (vazio se aberta)'>";
      html += "<label style='font-size:12px;color:#aaa;'>URL Cloud (neoface.dev.br):</label>";
      html += "<input name='cloud_url' value='" + safeCloud + "' placeholder='https://neoface.dev.br/jukebox'>";
      html += "<button type='submit'>Salvar e Conectar</button></form></body></html>";
      portalServer.send(200, "text/html", html);
    });

    portalServer.on("/save", HTTP_POST, []() {
      String s = portalServer.arg("ssid");
      String p = portalServer.arg("pass");
      String c = portalServer.arg("cloud_url");
      s.trim();
      c.trim();
      while (c.endsWith("/")) c.remove(c.length() - 1);
      if (c.length() == 0) c = DEFAULT_CLOUD_URL;

      if (s.length() > 0) {
        prefs.begin("jukebox-wifi", false);
        prefs.putString("ssid", s);
        prefs.putString("pass", p);
        prefs.putString("cloud_url", c);
        prefs.end();
        portalServer.send(200, "text/html", "<html><body style='background:#121212;color:#1db954;font-family:sans-serif;text-align:center;padding:40px;'><h2>Configurado!</h2><p>Reiniciando a vitrola...</p></body></html>");
        delay(1000);
        ESP.restart();
      } else {
        portalServer.send(400, "text/plain", "SSID invalido");
      }
    });

    portalServer.onNotFound([]() {
      portalServer.sendHeader("Location", "http://192.168.4.1/", true);
      portalServer.send(302, "text/plain", "");
    });
  }

  portalServer.begin();

  digitalWrite(PIN_RFID_SDA, HIGH);
  tft.fillScreen(COL_BG);
  tft.setCursor(20, 45);
  tft.setTextColor(COL_AMBER);
  tft.setTextSize(2);
  tft.print("CONFIGURAR WI-FI");
  tft.setTextSize(1);
  tft.setTextColor(COL_WHITE);
  tft.setCursor(20, 88);
  tft.print("1. Conecte o celular na rede:");
  tft.setTextColor(COL_SPOTIFY);
  tft.setCursor(20, 108);
  tft.setTextSize(2);
  tft.print("Vitrola-Jukebox");
  tft.setTextSize(1);
  tft.setTextColor(COL_WHITE);
  tft.setCursor(20, 148);
  tft.print("2. Abra o navegador em: 192.168.4.1");
  tft.setCursor(20, 175);
  tft.setTextColor(COL_MUTED);
  tft.printf("Redes encontradas: %d", n > 0 ? n : 0);
}

// ==============================================================================
// RESTAURACAO DE REGISTRADORES DO RC522 E CONTROLE DO MOTOR TT
// ==============================================================================
void reinitRfidRegisters(MFRC522::PCD_RxGain gain) {
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_RFID_RST, HIGH);

  // Se o RC522 sofreu reset por queda de tensao do motor, TxControlReg perde os bits 0x03 (antena)
  // ou TxASKReg perde 0x40 (100% ASK). Restauramos todos os registradores vitais sem delay longo:
  rfid.PCD_WriteRegister(rfid.TModeReg, 0x80);
  rfid.PCD_WriteRegister(rfid.TPrescalerReg, 0xA9);
  rfid.PCD_WriteRegister(rfid.TReloadRegH, 0x03);
  rfid.PCD_WriteRegister(rfid.TReloadRegL, 0xE8);
  rfid.PCD_WriteRegister(rfid.TxASKReg, 0x40);
  rfid.PCD_WriteRegister(rfid.ModeReg, 0x3D);
  rfid.PCD_WriteRegister(rfid.TxModeReg, 0x00);
  rfid.PCD_WriteRegister(rfid.RxModeReg, 0x00);
  rfid.PCD_WriteRegister(rfid.ModWidthReg, 0x26);
  rfid.PCD_SetAntennaGain(gain);
  rfid.PCD_AntennaOn();
}

void startMotorWithKick(int speed) {
  digitalWrite(PIN_MOTOR_IN2, LOW);

  // Rampa suave para evitar queda brusca de tensao que reseta a tela
  int target = constrain(speed, 20, 150);
  for (int p = 15; p <= target; p += 10) {
    analogWrite(PIN_MOTOR_IN1, p);
    delay(10);
  }
  analogWrite(PIN_MOTOR_IN1, target);
  delay(10);

  // Cura imediata caso o pico de partida do motor tenha reiniciado registradores do RC522
  reinitRfidRegisters(rfid.RxGain_max);
  digitalWrite(PIN_RFID_SDA, HIGH);
}

void stopMotor() {
  analogWrite(PIN_MOTOR_IN1, 0);
  digitalWrite(PIN_MOTOR_IN1, LOW);
  digitalWrite(PIN_MOTOR_IN2, LOW);
}

// ==============================================================================
// VERIFICACAO DE PRESENCA DA TAG NO PRATO (MULTI-GANHO + CARRIER POR)
// ==============================================================================
bool checkCardStillPresent(const char* targetUid) {
  digitalWrite(PIN_TFT_CS, HIGH);

  byte bufferATQA[2];
  byte bufferSize = sizeof(bufferATQA);
  MFRC522::StatusCode status;

  // Tentativa 1: Ganho Maximo (48dB - ideal para 5mm a 25mm)
  reinitRfidRegisters(rfid.RxGain_max);
  status = rfid.PICC_WakeupA(bufferATQA, &bufferSize);
  if (status != MFRC522::STATUS_OK) {
    bufferSize = sizeof(bufferATQA);
    status = rfid.PICC_RequestA(bufferATQA, &bufferSize);
  }

  // Tentativa 2: Ganho Medio (33dB - evita saturacao do receptor quando a tag esta colada a 0mm)
  if (status != MFRC522::STATUS_OK) {
    reinitRfidRegisters(rfid.RxGain_33dB);
    bufferSize = sizeof(bufferATQA);
    status = rfid.PICC_WakeupA(bufferATQA, &bufferSize);
    if (status != MFRC522::STATUS_OK) {
      bufferSize = sizeof(bufferATQA);
      status = rfid.PICC_RequestA(bufferATQA, &bufferSize);
    }
  }

  // Tentativa 3: Se falhou, pausa PWM do motor brevemente e cicla a antena RF (Power-On Reset na tag NTAG)
  if (status != MFRC522::STATUS_OK) {
    if (isPlaying) {
      analogWrite(PIN_MOTOR_IN1, 0);
      delay(15);
    }
    rfid.PCD_AntennaOff();
    delay(6);
    reinitRfidRegisters(rfid.RxGain_max);
    delay(6);

    bufferSize = sizeof(bufferATQA);
    status = rfid.PICC_RequestA(bufferATQA, &bufferSize);
    if (status != MFRC522::STATUS_OK) {
      bufferSize = sizeof(bufferATQA);
      status = rfid.PICC_WakeupA(bufferATQA, &bufferSize);
    }
    if (status != MFRC522::STATUS_OK) {
      reinitRfidRegisters(rfid.RxGain_33dB);
      bufferSize = sizeof(bufferATQA);
      status = rfid.PICC_RequestA(bufferATQA, &bufferSize);
    }

    if (isPlaying) {
      analogWrite(PIN_MOTOR_IN1, currentMotorSpeed);
    }
  }

  if (status == MFRC522::STATUS_OK) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    digitalWrite(PIN_RFID_SDA, HIGH);
    return true;
  }

  digitalWrite(PIN_RFID_SDA, HIGH);
  return false;
}

// ==============================================================================
// DOWNLOAD DOS FRAMES DA CAPA GIRATORIA (LAN OU CLOUD HTTPS)
// ==============================================================================
bool fetchCoverFrames() {
  if (WiFi.status() != WL_CONNECTED) return false;

  if (coverFramesBuffer == nullptr) {
    if (psramFound()) {
      coverFramesBuffer = (uint8_t*)ps_malloc(COVER_TOTAL_BYTES);
    } else {
      coverFramesBuffer = (uint8_t*)malloc(COVER_TOTAL_BYTES);
    }
    if (coverFramesBuffer == nullptr) {
      Serial.println("[COVER] Sem RAM/PSRAM para capas -> mantendo selo classico");
      return false;
    }
  }

  for (int attempt = 0; attempt < 2; attempt++) {
    HTTPClient http;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    String url = buildApiUrl("/api/cover/frames");
    beginHttpClient(http, plainClient, secureClient, url, 6000);

    int code = http.GET();
    if (code == 200) {
      WiFiClient* stream = http.getStreamPtr();
      int bytesRead = 0;
      uint32_t startWait = millis();
      while ((http.connected() || stream->available() > 0) && bytesRead < COVER_TOTAL_BYTES && millis() - startWait < 6000) {
        size_t avail = stream->available();
        if (avail > 0) {
          int r = stream->readBytes(coverFramesBuffer + bytesRead, min((size_t)(COVER_TOTAL_BYTES - bytesRead), avail));
          bytesRead += r;
          startWait = millis();
        } else {
          delay(5);
        }
      }
      http.end();

      if (bytesRead >= COVER_TOTAL_BYTES) {
        hasCoverFrames = true;
        lastCoverTrack = currentTrackTitle;
        Serial.println("[COVER] 8 frames da capa oficial baixados!");
        digitalWrite(PIN_RFID_SDA, HIGH);
        drawVinylVisualizer(vinylAnimStep);
        return true;
      }
      return false;
    }

    http.end();
    if (!useCloudServer && code <= 0) {
      Serial.println("[REDE] Falha LAN ao baixar capa -> alternando para CLOUD");
      useCloudServer = true;
      continue;
    }
    break;
  }
  return false;
}

// ==============================================================================
// DISCO COLOCADO / RETIRADO
// ==============================================================================
void handleDiscPlaced(const char* uidHex) {
  strncpy(activeUidHex, uidHex, sizeof(activeUidHex));
  isDiscPresent = true;
  missingChecksCount = 0;
  progressSeconds = 0;

  // Reseta buffer de audio I2S para a nova faixa
  __atomic_store_n(&audioRingTail, __atomic_load_n(&audioRingHead, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
  audioBuffering = true;

  Serial.printf("\n[RFID] Disco colocado! UID: %s\n", uidHex);

  String payload = "{\"uid\":\"" + String(uidHex) + "\",\"cloud\":" + String(useCloudServer ? "true" : "false") + "}";
  String resp;
  bool ok = sendServerPost("/api/disc/place", payload, resp);

  if (ok && resp.length() > 0) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);

    if (!err) {
      isDiscRegistered = doc["registered"] | false;
      const char* t = doc["title"] | (isDiscRegistered ? "Musica Spotify" : "DISCO NAO CADASTRADO");
      const char* a = doc["artist"] | (isDiscRegistered ? "Spotify" : "Cadastre no Painel Web");
      const char* d = doc["disc_name"] | "Disco Vinil";

      currentTrackTitle = String(t);
      currentArtist     = String(a);
      currentDiscName   = String(d);

      if (isDiscRegistered) {
        isPlaying = true;
        startMotorWithKick(currentMotorSpeed);
        Serial.printf("[PLAY] Tocando: %s - %s\n", currentTrackTitle.c_str(), currentArtist.c_str());
        // Baixa frames da capa do Spotify
        fetchCoverFrames();
      } else {
        isPlaying = false;
        stopMotor();
        hasCoverFrames = false;
        Serial.println("[AVISO] Tag virgem! Cadastre no painel web.");
      }
    }
  } else {
    currentTrackTitle = "Falha no Servidor STI";
    currentArtist     = "Verifique Wi-Fi/STI";
    isPlaying = false;
    stopMotor();
    hasCoverFrames = false;
  }

  digitalWrite(PIN_RFID_SDA, HIGH);
  drawFullUI();
}

void handleDiscRemoved() {
  stopMotor();
  isDiscPresent = false;
  isPlaying = false;
  isDiscRegistered = false;
  hasCoverFrames = false;
  missingChecksCount = 0;
  strncpy(activeUidHex, "NENHUM", sizeof(activeUidHex));

  // Corta o audio no falante instantaneamente
  __atomic_store_n(&audioRingTail, __atomic_load_n(&audioRingHead, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
  audioBuffering = true;

  currentDiscName   = "Prato livre";
  currentTrackTitle = "Aproxime um vinil";
  currentArtist     = "Vitrola Jukebox";
  progressSeconds   = 0;

  String dummyResp;
  sendServerPost("/api/disc/remove", "{}", dummyResp);

  digitalWrite(PIN_RFID_SDA, HIGH);
  drawFullUI();
}

// ==============================================================================
// CONTROLE VIA ENCODER ROTATIVO (VOLUME, PLAY/PAUSE E PORTAL WI-FI 3s)
// ==============================================================================
void handleEncoder() {
  static unsigned long lastEncTime = 0;
  int currentClk = digitalRead(PIN_ENC_CLK);

  if (currentClk != lastClkState && currentClk == LOW) {
    if (millis() - lastEncTime > 60) {
      lastEncTime = millis();
      String dummy;
      if (digitalRead(PIN_ENC_DT) != currentClk) {
        currentVolume = min(100, currentVolume + 5);
        sendServerPost("/api/player/vol_up", "{}", dummy);
        Serial.printf("[ENCODER] Volume: %d%%\n", currentVolume);
      } else {
        currentVolume = max(0, currentVolume - 5);
        sendServerPost("/api/player/vol_down", "{}", dummy);
        Serial.printf("[ENCODER] Volume: %d%%\n", currentVolume);
      }
      showVolumePopup = true;
      lastVolumePopupTime = millis();
      digitalWrite(PIN_RFID_SDA, HIGH);
      drawVolumePopup();
    }
  }
  lastClkState = currentClk;

  bool btnDown = (digitalRead(PIN_ENC_SW) == LOW);
  if (btnDown) {
    if (!encBtnWasDown) {
      encBtnWasDown = true;
      encBtnDownTime = millis();
      portalTriggeredByHold = false;
    } else if (!portalTriggeredByHold && (millis() - encBtnDownTime >= 3000)) {
      portalTriggeredByHold = true;
      Serial.println("[ENCODER] Segurado 3s -> Abrindo Portal Wi-Fi!");
      startCaptivePortal();
      return;
    }
  } else if (encBtnWasDown) {
    unsigned long pressDuration = millis() - encBtnDownTime;
    encBtnWasDown = false;
    if (!portalTriggeredByHold && pressDuration > 50 && pressDuration < 3000) {
      if (millis() - lastEncoderBtnPress > 250) {
        lastEncoderBtnPress = millis();
        String dummy;
        sendServerPost("/api/player/toggle", "{}", dummy);
        isPlaying = !isPlaying;
        if (!isPlaying) stopMotor();
        else if (isDiscPresent && isDiscRegistered) startMotorWithKick(currentMotorSpeed);
        digitalWrite(PIN_RFID_SDA, HIGH);
        drawTrackInfo();
        Serial.println("[ENCODER] Play/Pause clicado!");
      }
    }
  }
}

// ==============================================================================
// COMUNICACAO REST COM SERVIDOR STI (FAILOVER AUTOMATICO LAN -> CLOUD HTTPS)
// ==============================================================================
String buildApiUrl(const String& path) {
  if (useCloudServer) {
    return cloudServerUrl + path;
  }
  return "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) + path;
}

void beginHttpClient(HTTPClient& http, WiFiClient& plainClient, WiFiClientSecure& secureClient, const String& url, int timeoutMs) {
  http.setReuse(false);
  if (url.startsWith("https://")) {
    secureClient.setInsecure();
    http.begin(secureClient, url);
  } else {
    http.begin(plainClient, url);
  }
  http.setTimeout(timeoutMs);
  http.setUserAgent("VitrolaESP32/1.0");
}

bool sendServerPost(const String& path, const String& jsonPayload, String& responseOut) {
  if (WiFi.status() != WL_CONNECTED) return false;

  for (int attempt = 0; attempt < 2; attempt++) {
    HTTPClient http;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    String url = buildApiUrl(path);

    Serial.printf("[HTTP POST] Enviando para: %s (tentativa %d)\n", url.c_str(), attempt + 1);
    beginHttpClient(http, plainClient, secureClient, url, 5000);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(jsonPayload);
    Serial.printf("[HTTP POST] Resposta HTTP: %d\n", code);
    if (code > 0) {
      responseOut = http.getString();
      http.end();
      return (code == 200 || code == 204);
    }
    http.end();
    delay(200);
  }
  return false;
}

bool performHttpGet(const String& path, String& payloadOut, int timeoutMs) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  String url = buildApiUrl(path);

  beginHttpClient(http, plainClient, secureClient, url, timeoutMs < 3500 ? 3500 : timeoutMs);
  int code = http.GET();
  if (code == 200) {
    payloadOut = http.getString();
    http.end();
    return true;
  }
  http.end();
  return false;
}

void syncServerStatus() {
  String payload;
  if (!performHttpGet("/api/status", payload, useCloudServer ? 3000 : 1500)) {
    return;
  }

  JsonDocument doc;
  if (!deserializeJson(doc, payload)) {
    int serverProgress = doc["progress_sec"] | progressSeconds;
    int serverDuration = doc["duration_sec"] | durationSeconds;
    if (serverDuration > 0) durationSeconds = serverDuration;
    if (abs(serverProgress - progressSeconds) > 2) {
      progressSeconds = serverProgress;
    }

    currentVolume   = doc["volume"] | currentVolume;
    isPlaying       = doc["playing"] | isPlaying;

    const char* t = doc["title"] | "";
    if (strlen(t) > 0 && String(t) != currentTrackTitle) {
      currentTrackTitle = String(t);
      currentArtist     = String((const char*)(doc["artist"] | ""));
      digitalWrite(PIN_RFID_SDA, HIGH);
      drawTrackInfo();
      // Baixa a nova capa da faixa
      fetchCoverFrames();
    } else {
      digitalWrite(PIN_RFID_SDA, HIGH);
      drawProgressBar();
    }
  }
}

// ==============================================================================
// INTERFACE GRAFICA NO DISPLAY ILI9341 2.8" (320x240)
// ==============================================================================
void drawHeader() {
  tft.fillRect(0, 0, 320, 32, COL_CARD);
  tft.setCursor(10, 8);
  tft.setTextColor(COL_WHITE);
  tft.setTextSize(2);
  tft.print("VITROLA");

  tft.setCursor(105, 12);
  tft.setTextColor(COL_SPOTIFY);
  tft.setTextSize(1);
  tft.print("STI JUKEBOX");

  tft.setCursor(240, 12);
  tft.setTextColor(isWifiConnected ? COL_SPOTIFY : COL_RED);
  tft.print(isWifiConnected ? "WI-FI OK" : "SEM WI-FI");

  tft.drawFastHLine(0, 32, 320, COL_EDGE);
}

void drawVinylVisualizer(uint8_t step) {
  const int cx = 56, cy = 104, r = 40;

  // Corpo do vinil negro com ranhuras concentricas
  tft.fillCircle(cx, cy, r, COL_VINYL);
  tft.drawCircle(cx, cy, r, COL_EDGE);
  tft.drawCircle(cx, cy, r - 6, 0x18C3);
  tft.drawCircle(cx, cy, r - 11, 0x18C3);

  if (hasCoverFrames && coverFramesBuffer != nullptr && isPlaying) {
    // Desenha a capa real circular rotacionada no centro do disco
    uint8_t frameIdx = step % COVER_FRAMES_COUNT;
    uint16_t* frameData = (uint16_t*)(coverFramesBuffer + frameIdx * (COVER_FRAME_SIZE * COVER_FRAME_SIZE * 2));
    tft.drawRGBBitmap(cx - (COVER_FRAME_SIZE / 2), cy - (COVER_FRAME_SIZE / 2), frameData, COVER_FRAME_SIZE, COVER_FRAME_SIZE);

    // Contorno do selo e furo central do disco
    tft.drawCircle(cx, cy, (COVER_FRAME_SIZE / 2) - 1, COL_EDGE);
    tft.fillCircle(cx, cy, 3, COL_BG);
    tft.drawCircle(cx, cy, 3, COL_EDGE);
  } else {
    // Linhas dinamicas de reflexo de luz
    int dx = (step & 1) ? 14 : -14;
    int dy = (step & 2) ? 14 : -14;
    if (isDiscPresent && isPlaying) {
      tft.drawLine(cx - dx, cy - dy, cx + dx, cy + dy, COL_SPOTIFY);
    }

    // Selo central verde Spotify
    tft.fillCircle(cx, cy, 14, COL_SPOTIFY);
    tft.fillCircle(cx, cy, 3, COL_BG);
  }
}

void drawTrackInfo() {
  tft.fillRect(115, 45, 205, 100, COL_BG);

  // Nome do Disco / Tag
  tft.setCursor(115, 48);
  tft.setTextColor(isDiscRegistered ? COL_SPOTIFY : COL_AMBER);
  tft.setTextSize(1);
  tft.print(currentDiscName.substring(0, 26));

  // Titulo da Musica
  tft.setCursor(115, 68);
  tft.setTextColor(COL_WHITE);
  tft.setTextSize(2);
  String shortTitle = currentTrackTitle.substring(0, 16);
  tft.print(shortTitle);

  // Artista
  tft.setCursor(115, 96);
  tft.setTextColor(COL_MUTED);
  tft.setTextSize(1);
  String shortArtist = currentArtist.substring(0, 28);
  tft.print(shortArtist);

  // Status de Reproducao
  tft.setCursor(115, 118);
  tft.setTextColor(isPlaying ? COL_SPOTIFY : COL_AMBER);
  tft.print(isPlaying ? "● REPRODUZINDO" : "○ PAUSADO");

  drawProgressBar();
}

void drawProgressBar() {
  if (showVolumePopup) return;

  const int barX = 20, barY = 175, barW = 280, barH = 8;
  tft.fillRect(barX, barY - 20, barW, 40, COL_BG);

  tft.drawRoundRect(barX, barY, barW, barH, 4, COL_EDGE);

  float pct = (durationSeconds > 0) ? (float)progressSeconds / (float)durationSeconds : 0.0f;
  pct = constrain(pct, 0.0f, 1.0f);
  int fillW = (int)((barW - 4) * pct);
  if (fillW > 0) {
    tft.fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 2, COL_SPOTIFY);
  }

  tft.setTextColor(COL_MUTED);
  tft.setTextSize(1);
  tft.setCursor(barX, barY + 14);
  tft.printf("%d:%02d", progressSeconds / 60, progressSeconds % 60);

  tft.setCursor(barX + barW - 32, barY + 14);
  tft.printf("%d:%02d", durationSeconds / 60, durationSeconds % 60);
}

void drawVolumePopup() {
  const int barX = 20, barY = 175, barW = 280, barH = 8;
  tft.fillRect(barX, barY - 20, barW, 40, COL_BG);

  tft.setTextColor(COL_AMBER);
  tft.setTextSize(1);
  tft.setCursor(barX, barY - 12);
  tft.printf("VOLUME: %d%%", currentVolume);

  tft.drawRoundRect(barX, barY, barW, barH, 4, COL_EDGE);
  int fillW = (int)((barW - 4) * (currentVolume / 100.0f));
  if (fillW > 0) {
    tft.fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 2, COL_AMBER);
  }
}

void drawFullUI() {
  digitalWrite(PIN_RFID_SDA, HIGH);
  tft.fillScreen(COL_BG);
  drawHeader();
  drawVinylVisualizer(vinylAnimStep);
  drawTrackInfo();

  tft.drawFastHLine(0, 215, 320, COL_EDGE);
  tft.setCursor(15, 224);
  tft.setTextColor(COL_MUTED);
  tft.setTextSize(1);
  if (useCloudServer) {
    tft.printf("Modo: CLOUD neoface.dev.br | Tag: %s", activeUidHex);
  } else {
    tft.printf("Modo: LAN %s | Tag: %s", SERVER_HOST, activeUidHex);
  }
}

// ==============================================================================
// METODOS DE AUDIO I2S E STREAM TCP (MAX98357A + SPOTIFY CONNECT)
// ==============================================================================
static inline uint32_t audioRingUsed() {
  uint32_t h = __atomic_load_n(&audioRingHead, __ATOMIC_ACQUIRE);
  uint32_t t = __atomic_load_n(&audioRingTail, __ATOMIC_ACQUIRE);
  return h - t;
}

static inline uint32_t audioRingFree() {
  return AUDIO_RING_SIZE - audioRingUsed();
}

static void audioRingWrite(const uint8_t* src, uint32_t n) {
  uint32_t h = audioRingHead;
  uint32_t pos = h & AUDIO_RING_MASK;
  uint32_t first = min(n, (uint32_t)(AUDIO_RING_SIZE - pos));
  memcpy(audioRing + pos, src, first);
  if (n > first) memcpy(audioRing, src + first, n - first);
  __atomic_store_n(&audioRingHead, h + n, __ATOMIC_RELEASE);
}

static void audioRingRead(uint8_t* dst, uint32_t n) {
  uint32_t t = audioRingTail;
  uint32_t pos = t & AUDIO_RING_MASK;
  uint32_t first = min(n, (uint32_t)(AUDIO_RING_SIZE - pos));
  memcpy(dst, audioRing + pos, first);
  if (n > first) memcpy(dst + first, audioRing, n - first);
  __atomic_store_n(&audioRingTail, t + n, __ATOMIC_RELEASE);
}

void playToneMono(float freqHz, int durationMs, float amp) {
  const int chunkSamples = 256;
  alignas(4) int16_t buf[chunkSamples];
  int totalSamples = (SAMPLE_RATE * durationMs) / 1000;
  int fadeSamples  = min(totalSamples / 4, (SAMPLE_RATE * 15) / 1000);
  float gain = (currentVolume / 100.0f) * amp * 32000.0f;
  double phase = 0.0, inc = (2.0 * M_PI * freqHz) / (double)SAMPLE_RATE;
  int idx = 0;
  for (int i = 0; i < totalSamples; i++) {
    float env = 1.0f;
    if (i < fadeSamples) env = (float)i / fadeSamples;
    else if (i > totalSamples - fadeSamples) env = (float)(totalSamples - i) / fadeSamples;
    buf[idx++] = (int16_t)(sinf((float)phase) * env * gain);
    if (idx >= chunkSamples) { i2s.write((const uint8_t*)buf, idx * 2); idx = 0; }
    phase += inc;
    if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
  }
  if (idx > 0) i2s.write((const uint8_t*)buf, idx * 2);
}

void playStartupChime() {
  playToneMono(523.25f, 90, 0.06f);
  playToneMono(659.25f, 90, 0.06f);
  playToneMono(783.99f, 130, 0.08f);
}

void audioTask(void*) {
  alignas(4) static int16_t out[AUDIO_PLAY_CHUNK / 2];

  for (;;) {
    if (toneRequest) {
      playStartupChime();
      toneRequest = false;
    }

    uint32_t used = audioRingUsed();

    if (audioBuffering) {
      if (used >= AUDIO_PREFILL_BYTES) {
        audioBuffering = false;
      }
    } else if (used < 2) {
      audioBuffering = true;
      audioUnderruns++;
    }

    if (!audioBuffering && isPlaying) {
      uint32_t n = min(used, (uint32_t)AUDIO_PLAY_CHUNK) & ~1u;
      audioRingRead((uint8_t*)out, n);
      int count = n / 2;
      int vol = currentVolume;
      if (vol < 100) {
        for (int i = 0; i < count; i++) {
          out[i] = (int16_t)(((int32_t)out[i] * vol) / 100);
        }
      }
      i2s.write((const uint8_t*)out, n);
    } else {
      memset(out, 0, sizeof(out));
      i2s.write((const uint8_t*)out, 256 * 2);
    }
  }
}

void connectAudioStream() {
  if (useCloudServer) return;
  Serial.printf("[AUDIO] Conectando na ponte TCP %s:%d...\n", SERVER_HOST, AUDIO_PORT);
  if (audioClient.connect(SERVER_HOST, AUDIO_PORT, 2000)) {
    audioClient.setNoDelay(true);
    if (audioRingHead & 1) { uint8_t z = 0; audioRingWrite(&z, 1); }
    Serial.println("[AUDIO] Stream TCP conectado!");
  } else {
    Serial.println("[AUDIO] Falha ao conectar na ponte TCP.");
  }
}

void pumpNetworkAudio() {
  if (!audioClient.connected()) return;
  static uint8_t tmp[2048];
  for (int i = 0; i < 4; i++) {
    int avail = audioClient.available();
    uint32_t space = audioRingFree();
    if (avail <= 0 || space == 0) break;
    int toRead = min((uint32_t)avail, min(space, (uint32_t)sizeof(tmp)));
    int r = audioClient.read(tmp, toRead);
    if (r <= 0) break;
    audioRingWrite(tmp, r);
    audioBytesRecebidos += r;
  }
}

void audioNetTask(void*) {
  for (;;) {
    if (isWifiConnected && !isCaptivePortalOpen && !useCloudServer) {
      if (!audioClient.connected()) {
        if (millis() - lastAudioReconnect >= 3000) {
          lastAudioReconnect = millis();
          connectAudioStream();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
      } else {
        pumpNetworkAudio();
        if (audioRingFree() < 2048) {
          vTaskDelay(pdMS_TO_TICKS(10));
        } else {
          vTaskDelay(pdMS_TO_TICKS(2));
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}
