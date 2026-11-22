#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>

// ==============================================================================
// REDE WI-FI E SERVIDOR SPOTIFY
// ==============================================================================
#define LOCAL_SERVER_HOST "192.168.0.105"
#define LOCAL_SERVER_PORT 3003

#define GLOBAL_HOST       "neoface.dev.br"
#define GLOBAL_PORT       443
#define GLOBAL_STATUS_URL "/jukebox/api/status"
#define GLOBAL_COVER_URL  "/jukebox/api/cover/frames"

// ==============================================================================
// PINAGEM DO DISPLAY CYD 2-USB (ESP32-2432S028R)
// ==============================================================================
#define PIN_TFT_BL        21
#define PIN_TOUCH_CS      33

// Pinos do Slot MicroSD Onboard da CYD (Barramento VSPI)
#define PIN_SD_CS         5
#define PIN_SD_SCK        18
#define PIN_SD_MISO       19
#define PIN_SD_MOSI       23

// Pinos do LED RGB Onboard da CYD (Ativos em LOW)
#define PIN_RGB_R         4
#define PIN_RGB_G         16
#define PIN_RGB_B         17

// ==============================================================================
// INSTANCIAS DE HARDWARE
// ==============================================================================
TFT_eSPI tft = TFT_eSPI();
SPIClass sdSPI(VSPI);
bool hasSdCard            = false;
uint64_t sdCardSizeBytes  = 0;
WiFiMulti* cydWifiMulti = nullptr;
Preferences cydPrefs;
WiFiClientSecure cloudClient;

// Cores Oficiais
#define COL_BG            TFT_BLACK
#define COL_WHITE         TFT_WHITE
#define COL_MUTED         0x8410
#define COL_EDGE          0x2965
#define COL_SPOTIFY       0x1E46  // #1DB954 Verde Spotify Oficial
#define COL_AMBER         0xFDE0  // Amarelo / Dourado vintage
#define COL_VINYL         0x1082

// ==============================================================================
// ESTADO DO DISPLAY E REPRODUCAO
// ==============================================================================
bool isPlaying            = false;
bool isDiscPresent        = false;
bool isApScreen           = false;
bool isCloudMode          = false;
int lanFailures           = 0;

String currentTrackTitle  = "Aproxime um vinil";
String currentArtist      = "Vitrola Jukebox";
String currentDiscName    = "Prato livre";
String activeUidHex       = "NENHUM";

int progressSeconds       = 0;
int durationSeconds       = 180;
int currentVolume         = 40;
uint8_t currentRotation   = 1; // 1 = Paisagem Correta

uint8_t vinylAnimStep     = 0;
unsigned long lastAnimTick   = 0;
unsigned long lastWifiPoll   = 0;

// Capa Real do Album (8 quadros rotativos de 56x56 pixels em RGB565)
static uint16_t coverFrames[8][56 * 56];
bool hasCoverFrames       = false;
String lastLoadedCoverTrack = "";

// Gestao de Audio Direto do MicroSD para o ESP32-S3 via UART (1.5 Mbps)
File localAudioFile;
bool isLocalAudioStreaming    = false;
String currentStreamingUid    = "";
unsigned long lastAudioPacketUs = 0;
unsigned long lastDiscsSync   = 0;

// ==============================================================================
// PROTOTIPOS
// ==============================================================================
void drawFullUI();
void drawHeader();
void drawVinylVisualizer(uint8_t step);
void drawTrackInfo();
void drawProgressBar();
void drawApSetupScreen(const String& apSsid, const String& apIp);
void processSerialCommand(const String& line);
void pollServerStatus();
void reloadCydWifiMulti();
String getCydWifiDatabaseJson();
void fetchCoverFrames();
String getLocalAudioPath(const String& uid);
void startLocalAudio(const String& uid);
void stopLocalAudio();
void syncDiscsJsonFromHost();

// ==============================================================================
// BANCO DE DADOS WI-FI CYD
// ==============================================================================
String getCydWifiDatabaseJson() {
  cydPrefs.begin("jukebox_cyd", false);
  String json = cydPrefs.getString("wifi_db", "");
  cydPrefs.end();

  if (json.length() == 0 || json == "[]") {
    // Insira aqui sua rede padrao ou sincronize via Serial com o S3
    json = "[{\"s\":\"SUA_REDE_WIFI\",\"p\":\"SUA_SENHA_WIFI\"}]";
    cydPrefs.begin("jukebox_cyd", false);
    cydPrefs.putString("wifi_db", json);
    cydPrefs.end();
  }
  return json;
}

void reloadCydWifiMulti() {
  if (cydWifiMulti) {
    delete cydWifiMulti;
  }
  cydWifiMulti = new WiFiMulti();

  String json = getCydWifiDatabaseJson();
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (!err && doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
      const char* s = obj["s"];
      const char* p = obj["p"];
      if (s && strlen(s) > 0) {
        cydWifiMulti->addAP(s, (p && strlen(p) > 0) ? p : "");
      }
    }
  }
}

// ==============================================================================
// SETUP
// ==============================================================================
void setup() {
  Serial.begin(1500000);
  Serial.setTxBufferSize(2048);
  delay(100);

  Serial.println("\n========================================================");
  Serial.println("  PAINEL GRAFICO OFICIAL TFT_eSPI - CYD 2-USB (ST7789)");
  Serial.println("========================================================");

  // 1. Apaga LED RGB Onboard
  pinMode(PIN_RGB_R, OUTPUT); digitalWrite(PIN_RGB_R, HIGH);
  pinMode(PIN_RGB_G, OUTPUT); digitalWrite(PIN_RGB_G, HIGH);
  pinMode(PIN_RGB_B, OUTPUT); digitalWrite(PIN_RGB_B, HIGH);

  // 2. Inicializa Display com TFT_eSPI (Suporte nativo a ST7789 e Touch XPT2046)
  tft.init();
  tft.invertDisplay(true); // Inversao desligada (Fundo preto solido e verde Spotify)
  tft.setRotation(currentRotation); // Rotação 1: Paisagem 320x240 sem fantasmas
  tft.fillScreen(COL_BG);

  // 3. Liga iluminacao da tela
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);

  // 4. Inicializa Slot de Cartao MicroSD Onboard (Barramento VSPI)
  sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (SD.begin(PIN_SD_CS, sdSPI, 20000000)) {
    hasSdCard = true;
    sdCardSizeBytes = SD.cardSize();
    uint8_t cardType = SD.cardType();
    Serial.printf("[SD OK] Cartao MicroSD detectado! Tipo: %d, Capacidade: %llu MB\n",
                  cardType, sdCardSizeBytes / (1024 * 1024));
    if (!SD.exists("/covers")) SD.mkdir("/covers");
    if (!SD.exists("/audio"))  SD.mkdir("/audio");
  } else {
    Serial.println("[SD] Nenhum cartao MicroSD detectado ou falha na montagem.");
  }

  // 5. Desenha UI completa
  drawFullUI();

  // 5. Carrega Banco de Dados de Redes e conecta Wi-Fi
  cloudClient.setInsecure();
  reloadCydWifiMulti();
  WiFi.mode(WIFI_STA);

  Serial.printf("[CYD] Painel ST7789 inicializado com sucesso! (W: %d, H: %d)\n", tft.width(), tft.height());
}

// ==============================================================================
// LOOP PRINCIPAL
// ==============================================================================
void loop() {
  unsigned long now = millis();

  // 1. Leitura de Telemetria via Serial do ESP32-S3
  while (Serial.available() > 0) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      processSerialCommand(line);
    }
  }

  // 1b. Transmissao continua de Audio do Cartao MicroSD para o ESP32-S3 via UART (1.5 Mbps)
  if (isLocalAudioStreaming && isPlaying && localAudioFile) {
    if (localAudioFile.available() > 0) {
      unsigned long nowUs = micros();
      // 512 bytes de PCM 16-bit 44.1kHz mono = 5805 us.
      // Ritmo de 5400 us mantem a PSRAM do S3 sempre alimentada com estabilidade absoluta.
      if (nowUs - lastAudioPacketUs >= 5400) {
        lastAudioPacketUs = nowUs;
        uint8_t audioChunk[512];
        int bytesRead = localAudioFile.read(audioChunk, sizeof(audioChunk));
        if (bytesRead > 0) {
          uint8_t hdr[4] = {
            0xAA,
            0x55,
            (uint8_t)((bytesRead >> 8) & 0xFF),
            (uint8_t)(bytesRead & 0xFF)
          };
          Serial.write(hdr, 4);
          Serial.write(audioChunk, bytesRead);
        }
      }
    } else {
      stopLocalAudio();
    }
  }

  // Se a tela estiver mostrando o aviso de Modo Setup AP, nao renderiza player
  if (isApScreen) {
    delay(20);
    return;
  }

  // 2. Leitura do Touchscreen Resistivo XPT2046
  uint16_t tx = 0, ty = 0;
  static unsigned long lastTouchTime = 0;
  if (tft.getTouch(&tx, &ty)) {
    if (now - lastTouchTime > 400) {
      lastTouchTime = now;
      Serial.printf("[CYD TOUCH] Toque em X:%d, Y:%d\n", tx, ty);

      // Toque no disco de vinil (Play/Pause)
      if (tx < 115 && ty > 35 && ty < 180) {
        Serial.println("{\"cmd\":\"PLAYER_TOGGLE\"}");
      }
      // Toque no canto superior direito (Proxima Musica)
      else if (tx > 260 && ty < 110) {
        Serial.println("{\"cmd\":\"PLAYER_NEXT\"}");
      }
      // Toque no canto inferior direito (Musica Anterior)
      else if (tx > 260 && ty >= 110 && ty < 210) {
        Serial.println("{\"cmd\":\"PLAYER_PREV\"}");
      }
    }
  }

  // 3. Mantem Wi-Fi e Sincronizacao Sem Fio
  if (WiFi.status() == WL_CONNECTED) {
    if (now - lastWifiPoll >= 1200) {
      lastWifiPoll = now;
      pollServerStatus();
    }
    if (now - lastDiscsSync >= 45000) {
      lastDiscsSync = now;
      syncDiscsJsonFromHost();
    }
  } else {
    static unsigned long lastCydReconnect = 0;
    if (now - lastCydReconnect >= 4000) {
      lastCydReconnect = now;
      if (cydWifiMulti) cydWifiMulti->run();
    }
  }

  // 4. Animacao do Vinil Girando suavemente (com Capa Real do Album!)
  if (now - lastAnimTick >= 350) {
    lastAnimTick = now;
    if (isDiscPresent && isPlaying) {
      vinylAnimStep = (vinylAnimStep + 1) % 8;
      drawVinylVisualizer(vinylAnimStep);
    }
  }
}

// ==============================================================================
// DOWNLOAD DOS QUADROS DA CAPA DO SPOTIFY
// ==============================================================================
void fetchCoverFrames() {
  if (currentTrackTitle == "Aproxime um vinil" || currentTrackTitle == "Aproxime um disco") {
    hasCoverFrames = false;
    return;
  }

  // 1. Tenta carregar do cache offline do cartao MicroSD
  String coverFile = "/covers/" + activeUidHex + ".bin";
  if (hasSdCard && activeUidHex != "NENHUM" && SD.exists(coverFile)) {
    File f = SD.open(coverFile, FILE_READ);
    if (f) {
      size_t readLen = f.read((uint8_t*)coverFrames, sizeof(coverFrames));
      f.close();
      if (readLen == sizeof(coverFrames)) {
        hasCoverFrames = true;
        lastLoadedCoverTrack = currentTrackTitle;
        Serial.printf("[SD CACHE] Capa carregada do MicroSD: %s (%d bytes)\n", coverFile.c_str(), (int)readLen);
        return;
      }
    }
  }

  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient* c = nullptr;
  WiFiClient plain;
  String host;
  int port;
  if (!isCloudMode) {
    c = &plain;
    host = LOCAL_SERVER_HOST;
    port = LOCAL_SERVER_PORT;
  } else {
    c = &cloudClient;
    host = GLOBAL_HOST;
    port = GLOBAL_PORT;
  }

  String path = isCloudMode ? String(GLOBAL_COVER_URL) : String("/api/cover/frames");
  if (c->connect(host.c_str(), port, 1500)) {
    c->print(String("GET ") + path + " HTTP/1.1\r\n" +
             "Host: " + host + "\r\n" +
             "Connection: close\r\n\r\n");

    unsigned long start = millis();
    while (!c->available() && millis() - start < 1500) {
      delay(5);
    }
    while (c->available()) {
      String h = c->readStringUntil('\n');
      if (h == "\r" || h.length() == 0) break;
    }

    int totalBytes = sizeof(coverFrames);
    uint8_t* ptr = (uint8_t*)coverFrames;
    int readBytes = 0;
    start = millis();
    while (readBytes < totalBytes && millis() - start < 2500) {
      int avail = c->available();
      if (avail > 0) {
        int r = c->read(ptr + readBytes, min(avail, totalBytes - readBytes));
        if (r > 0) readBytes += r;
      } else {
        delay(5);
      }
    }
    c->stop();

    if (readBytes == totalBytes) {
      hasCoverFrames = true;
      lastLoadedCoverTrack = currentTrackTitle;

      // 2. Salva no cartao MicroSD para uso offline futuro
      if (hasSdCard && activeUidHex != "NENHUM") {
        File f = SD.open(coverFile, FILE_WRITE);
        if (f) {
          f.write((const uint8_t*)coverFrames, sizeof(coverFrames));
          f.close();
          Serial.printf("[SD CACHE] Capa salva no MicroSD para uso offline: %s\n", coverFile.c_str());
        }
      }
    } else {
      hasCoverFrames = false;
    }
  }
}

// ==============================================================================
// SINCRONIZACAO COM O SERVIDOR (LAN LOCAL VS NUVEM GLOBAL)
// ==============================================================================
void pollServerStatus() {
  if (WiFi.status() != WL_CONNECTED) return;

  String payload = "";

  // 1. Tenta conexao local rapida
  if (!isCloudMode) {
    WiFiClient client;
    if (client.connect(LOCAL_SERVER_HOST, LOCAL_SERVER_PORT, 500)) {
      client.print(String("GET /api/status HTTP/1.1\r\n") +
                   "Host: " + LOCAL_SERVER_HOST + "\r\n" +
                   "Connection: close\r\n\r\n");

      unsigned long start = millis();
      while (!client.available() && millis() - start < 500) {
        delay(5);
      }
      while (client.available()) {
        String header = client.readStringUntil('\n');
        if (header == "\r" || header.length() == 0) break;
      }
      payload = client.readString();
      client.stop();
      lanFailures = 0;
    } else {
      lanFailures++;
      if (lanFailures >= 2) {
        isCloudMode = true;
      }
    }
  }

  // 2. Fallback para Nuvem Global (Fora de Casa)
  if (isCloudMode) {
    if (cloudClient.connect(GLOBAL_HOST, GLOBAL_PORT, 2000)) {
      cloudClient.print(String("GET ") + GLOBAL_STATUS_URL + " HTTP/1.1\r\n" +
                        "Host: " + GLOBAL_HOST + "\r\n" +
                        "Connection: close\r\n\r\n");

      unsigned long start = millis();
      while (!cloudClient.available() && millis() - start < 2000) {
        delay(10);
      }
      while (cloudClient.available()) {
        String header = cloudClient.readStringUntil('\n');
        if (header == "\r" || header.length() == 0) break;
      }
      payload = cloudClient.readString();
      cloudClient.stop();
    } else {
      isCloudMode = false;
      lanFailures = 0;
    }
  }

  if (payload.length() > 0) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      String newTitle   = doc["title"] | currentTrackTitle;
      String newArtist  = doc["artist"] | currentArtist;
      String newDisc    = doc["disc_name"] | currentDiscName;
      String newUid     = doc["active_uid"] | activeUidHex;
      bool newPlaying   = doc["playing"] | false;
      bool newPresent   = (newUid != "NENHUM" && newUid.length() > 0);
      int newProg       = doc["progress_sec"] | progressSeconds;
      int newDur        = doc["duration_sec"] | durationSeconds;
      int newVol        = doc["volume"] | currentVolume;

      bool needRedraw = false;
      if (newTitle != currentTrackTitle || newArtist != currentArtist ||
          newDisc != currentDiscName || newPlaying != isPlaying ||
          newPresent != isDiscPresent || newUid != activeUidHex) {
        needRedraw = true;
      }

      bool titleChanged = (newTitle != currentTrackTitle && newTitle.length() > 0);
      String prevUid    = activeUidHex;

      currentTrackTitle = newTitle;
      currentArtist     = newArtist;
      currentDiscName   = newDisc;
      activeUidHex      = newUid;
      isPlaying         = newPlaying;
      isDiscPresent     = newPresent;
      progressSeconds   = newProg;
      durationSeconds   = newDur;
      currentVolume     = newVol;

      if (activeUidHex != prevUid) {
        if (activeUidHex != "NENHUM" && activeUidHex.length() > 0 && isPlaying && !isLocalAudioStreaming) {
          startLocalAudio(activeUidHex);
        } else if (activeUidHex == "NENHUM" || activeUidHex.length() == 0) {
          stopLocalAudio();
        }
      }

      if (titleChanged && currentTrackTitle != lastLoadedCoverTrack) {
        fetchCoverFrames();
      }

      if (needRedraw) {
        drawTrackInfo();
        drawVinylVisualizer(vinylAnimStep);
        tft.fillRect(0, 215, 320, 25, COL_BG);
        tft.drawFastHLine(0, 215, 320, COL_EDGE);
        tft.setCursor(15, 224);
        tft.setTextColor(COL_MUTED, COL_BG);
        tft.setTextSize(1);
        tft.printf("%s | %s | Tag: %s", isCloudMode ? "Modo: NUVEM" : "Modo: LAN", hasSdCard ? "SD: OK" : "SD: --", activeUidHex.c_str());
      }
    }
  }
}

// ==============================================================================
// PROCESSA TELEMETRIA SERIAL VINDA DO ESP32-S3
// ==============================================================================
void processSerialCommand(const String& line) {
  if (line.startsWith("{")) {
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (!err) {
      if (doc.containsKey("cmd")) {
        String cmd = doc["cmd"].as<String>();

        if (cmd == "WIFI_AP_MODE") {
          bool active = doc["active"] | false;
          if (active) {
            String ssid = doc["ssid"] | "Vitrola-Jukebox-Setup";
            String ip   = doc["ip"]   | "192.168.4.1";
            drawApSetupScreen(ssid, ip);
          } else {
            isApScreen = false;
            drawFullUI();
          }
          return;
        }

        if (cmd == "WIFI_CONNECTED") {
          if (isApScreen) {
            isApScreen = false;
            drawFullUI();
          }
          return;
        }

        if (cmd == "SYNC_WIFI_DB") {
          JsonVariant db = doc["db"];
          String dbStr;
          serializeJson(db, dbStr);
          cydPrefs.begin("jukebox_cyd", false);
          cydPrefs.putString("wifi_db", dbStr);
          cydPrefs.end();
          reloadCydWifiMulti();
          return;
        }
      }

      String prevTitle   = currentTrackTitle;
      String prevUid     = activeUidHex;
      bool   prevPlaying = isPlaying;

      if (doc.containsKey("title"))    currentTrackTitle  = doc["title"].as<String>();
      if (doc.containsKey("artist"))   currentArtist      = doc["artist"].as<String>();
      if (doc.containsKey("disc"))     currentDiscName    = doc["disc"].as<String>();
      if (doc.containsKey("uid"))      activeUidHex       = doc["uid"].as<String>();
      if (doc.containsKey("playing"))  isPlaying          = doc["playing"].as<bool>();
      if (doc.containsKey("present"))  isDiscPresent      = doc["present"].as<bool>();
      if (doc.containsKey("progress")) progressSeconds    = doc["progress"].as<int>();
      if (doc.containsKey("duration")) durationSeconds    = doc["duration"].as<int>();
      if (doc.containsKey("vol"))      currentVolume      = doc["vol"].as<int>();

      // Gestao de Streaming Local direto do Cartao SD (SD-First Playback)
      if (activeUidHex != prevUid) {
        if (activeUidHex != "NENHUM" && activeUidHex.length() > 0 && isPlaying) {
          startLocalAudio(activeUidHex);
        } else {
          stopLocalAudio();
        }
      } else if (isPlaying != prevPlaying) {
        if (isPlaying && isDiscPresent && activeUidHex != "NENHUM" && !isLocalAudioStreaming) {
          startLocalAudio(activeUidHex);
        }
      }

      if (currentTrackTitle != prevTitle && currentTrackTitle.length() > 0 && currentTrackTitle != lastLoadedCoverTrack) {
        fetchCoverFrames();
      }

      if (!isApScreen) {
        drawTrackInfo();
        drawVinylVisualizer(vinylAnimStep);
      }
      return;
    }
  }

  if (line.startsWith("ROT:")) {
    currentRotation = line.substring(4).toInt();
    tft.setRotation(currentRotation);
    drawFullUI();
  }

  if (line.startsWith("INV:")) {
    bool inv = line.substring(4).toInt() > 0;
    tft.invertDisplay(inv);
  }
}

// ==============================================================================
// DESENHO DOS COMPONENTES VISUAIS
// ==============================================================================
void drawApSetupScreen(const String& apSsid, const String& apIp) {
  isApScreen = true;
  tft.fillScreen(COL_BG);

  // Topo Verde Spotify
  tft.fillRect(0, 0, 320, 34, COL_SPOTIFY);
  tft.setCursor(30, 8);
  tft.setTextColor(COL_BG, COL_SPOTIFY);
  tft.setTextSize(2);
  tft.print("CONFIGURAR WI-FI");

  // Moldura Central
  tft.drawRoundRect(10, 44, 300, 160, 8, COL_AMBER);

  tft.setCursor(20, 56);
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setTextSize(1);
  tft.print("1. Conecte seu celular no Wi-Fi:");

  tft.setCursor(30, 74);
  tft.setTextColor(COL_AMBER, COL_BG);
  tft.setTextSize(2);
  tft.print(apSsid);

  tft.setCursor(20, 106);
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setTextSize(1);
  tft.print("2. Abra no navegador do celular:");

  tft.setCursor(30, 124);
  tft.setTextColor(COL_SPOTIFY, COL_BG);
  tft.setTextSize(2);
  tft.printf("http://%s", apIp.c_str());

  tft.setCursor(20, 156);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.setTextSize(1);
  tft.print("3. Escolha a nova rede e digite a senha.");

  tft.setCursor(20, 178);
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.print("-> A vitrola conectara automaticamente!");

  // Rodape
  tft.drawFastHLine(0, 215, 320, COL_EDGE);
  tft.setCursor(15, 224);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.print("Aguardando configuracao pelo celular...");
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 32, COL_BG);

  tft.fillCircle(14, 16, 7, COL_SPOTIFY);
  tft.fillCircle(14, 16, 2, COL_BG);

  tft.setCursor(28, 12);
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setTextSize(1);
  tft.print("VITROLA JUKEBOX");

  tft.setCursor(140, 12);
  tft.setTextColor(COL_SPOTIFY, COL_BG);
  tft.print(hasSdCard ? "HI-FI [SD]" : "SPOTIFY HI-FI");

  tft.setCursor(248, 12);
  tft.setTextColor(COL_AMBER, COL_BG);
  tft.printf("VOL %d%%", currentVolume);

  tft.drawFastHLine(0, 32, 320, COL_EDGE);
}

void drawVinylVisualizer(uint8_t step) {
  const int cx = 56, cy = 104, r = 40;

  // Corpo do vinil negro com ranhuras concentricas
  tft.fillCircle(cx, cy, r, COL_VINYL);
  tft.drawCircle(cx, cy, r, COL_EDGE);
  tft.drawCircle(cx, cy, r - 6, 0x18C3);
  tft.drawCircle(cx, cy, r - 11, 0x18C3);

  // Se houver quadros da capa baixados, desenha a capa do album girando!
  if (hasCoverFrames) {
    tft.setSwapBytes(true);
    tft.pushImage(cx - 28, cy - 28, 56, 56, (uint16_t*)coverFrames[step % 8]);
    tft.setSwapBytes(false);
    tft.drawCircle(cx, cy, 28, COL_EDGE);
    tft.fillCircle(cx, cy, 4, COL_BG);
    tft.drawCircle(cx, cy, 4, COL_EDGE);
  } else {
    int dx = (step & 1) ? 14 : -14;
    int dy = (step & 2) ? 14 : -14;
    if (isDiscPresent && isPlaying) {
      tft.drawLine(cx - dx, cy - dy, cx + dx, cy + dy, COL_SPOTIFY);
    }
    tft.fillCircle(cx, cy, 14, COL_SPOTIFY);
    tft.fillCircle(cx, cy, 3, COL_BG);
  }
}

void drawTrackInfo() {
  tft.fillRect(115, 42, 205, 100, COL_BG);

  // Nome do Disco / Tag
  tft.setCursor(115, 45);
  tft.setTextColor(isDiscPresent ? COL_SPOTIFY : COL_AMBER, COL_BG);
  tft.setTextSize(1);
  tft.print(currentDiscName.substring(0, 24));

  // Botoes Touch discretos no canto direito (Next / Prev)
  tft.setCursor(295, 45);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.print(">>");

  tft.setCursor(295, 115);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.print("<<");

  // Titulo da Musica
  tft.setCursor(115, 65);
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setTextSize(2);
  String shortTitle = currentTrackTitle.substring(0, 15);
  tft.print(shortTitle);

  // Artista
  tft.setCursor(115, 93);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.setTextSize(1);
  String shortArtist = currentArtist.substring(0, 26);
  tft.print(shortArtist);

  // Status de Reproducao
  tft.setCursor(115, 115);
  tft.setTextColor(isPlaying ? COL_SPOTIFY : COL_AMBER, COL_BG);
  tft.print(isPlaying ? "● REPRODUZINDO" : "○ PAUSADO");

  drawProgressBar();
}

void drawProgressBar() {
  const int barX = 20, barY = 175, barW = 280, barH = 8;
  tft.fillRect(barX, barY - 20, barW, 40, COL_BG);

  tft.drawRoundRect(barX, barY, barW, barH, 4, COL_EDGE);

  float pct = (durationSeconds > 0) ? (float)progressSeconds / (float)durationSeconds : 0.0f;
  pct = constrain(pct, 0.0f, 1.0f);
  int fillW = (int)((barW - 4) * pct);
  if (fillW > 0) {
    tft.fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 2, COL_SPOTIFY);
  }

  tft.setTextColor(COL_MUTED, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(barX, barY + 14);
  tft.printf("%d:%02d", progressSeconds / 60, progressSeconds % 60);

  tft.setCursor(barX + barW - 32, barY + 14);
  tft.printf("%d:%02d", durationSeconds / 60, durationSeconds % 60);
}

void drawFullUI() {
  tft.fillScreen(COL_BG);
  drawHeader();
  drawVinylVisualizer(vinylAnimStep);
  drawTrackInfo();

  tft.drawFastHLine(0, 215, 320, COL_EDGE);
  tft.setCursor(15, 224);
  tft.setTextColor(COL_MUTED, COL_BG);
  tft.setTextSize(1);
  tft.printf("PAINEL CYD TOUCH | %s | Tag: %s", hasSdCard ? "SD: OK" : "SD: --", activeUidHex.c_str());
}

// ==============================================================================
// STREAMING DE AUDIO LOCAL DIRETO DO MICROSD PARA O ESP32-S3 VIA UART (1.5 Mbps)
// ==============================================================================
String getLocalAudioPath(const String& uid) {
  if (!hasSdCard || uid.length() == 0 || uid == "NENHUM") return "";

  // 1. Arquivo direto com o UID (ex: /audio/043963AF8B2681.s16)
  String directPath = "/audio/" + uid + ".s16";
  if (SD.exists(directPath)) {
    return directPath;
  }

  // 2. Busca pelo mapeamento no /discs.json (raiz do cartao MicroSD)
  if (SD.exists("/discs.json")) {
    File f = SD.open("/discs.json", FILE_READ);
    if (f) {
      DynamicJsonDocument doc(4096);
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (!err && doc.containsKey(uid)) {
        String uri = doc[uid]["uri"] | "";
        if (uri.startsWith("spotify:track:")) {
          String trackId = uri.substring(14);
          String trackPath = "/audio/" + trackId + ".s16";
          if (SD.exists(trackPath)) {
            return trackPath;
          }
        }
      }
    }
  }

  // 3. Fallback no /discos.json
  if (SD.exists("/discos.json")) {
    File f = SD.open("/discos.json", FILE_READ);
    if (f) {
      DynamicJsonDocument doc(4096);
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (!err && doc.containsKey(uid)) {
        String uri = doc[uid]["uri"] | "";
        if (uri.startsWith("spotify:track:")) {
          String trackId = uri.substring(14);
          String trackPath = "/audio/" + trackId + ".s16";
          if (SD.exists(trackPath)) {
            return trackPath;
          }
        }
      }
    }
  }

  return "";
}

void startLocalAudio(const String& uid) {
  if (!hasSdCard || uid.length() == 0 || uid == "NENHUM") return;

  String path = getLocalAudioPath(uid);
  if (path.length() == 0) {
    // Faixa nao encontrada no MicroSD: S3 usara streaming via Wi-Fi automaticamente
    return;
  }

  if (localAudioFile) localAudioFile.close();
  localAudioFile = SD.open(path, FILE_READ);
  if (localAudioFile && localAudioFile.available() > 0) {
    isLocalAudioStreaming = true;
    currentStreamingUid = uid;
    Serial.println("{\"cmd\":\"LOCAL_AUDIO_START\"}");
    Serial.flush();
    lastAudioPacketUs = micros();
    Serial.printf("[SD AUDIO] >> INICIANDO STREAM LOCAL: %s (%u bytes) <<\n", path.c_str(), localAudioFile.size());

    // Pre-carrega os primeiros 16 KB (32 blocos de 512 bytes) na PSRAM do S3 para inicio instantaneo
    for (int i = 0; i < 32 && localAudioFile.available() > 0; i++) {
      uint8_t buf[512];
      int r = localAudioFile.read(buf, sizeof(buf));
      if (r > 0) {
        uint8_t hdr[4] = {
          0xAA,
          0x55,
          (uint8_t)((r >> 8) & 0xFF),
          (uint8_t)(r & 0xFF)
        };
        Serial.write(hdr, 4);
        Serial.write(buf, r);
      }
    }
  }
}

void stopLocalAudio() {
  if (isLocalAudioStreaming || localAudioFile) {
    if (localAudioFile) localAudioFile.close();
    isLocalAudioStreaming = false;
    currentStreamingUid = "";
    Serial.println("{\"cmd\":\"LOCAL_AUDIO_END\"}");
    Serial.flush();
    Serial.println("[SD AUDIO] Stream local finalizado.");
  }
}

void syncDiscsJsonFromHost() {
  if (!hasSdCard || WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url;
  if (isCloudMode) {
    url = "https://" + String(GLOBAL_HOST) + "/jukebox/api/discs";
  } else {
    url = "http://" + String(LOCAL_SERVER_HOST) + ":" + String(LOCAL_SERVER_PORT) + "/api/discs";
  }
  http.begin(url);
  http.setTimeout(2500);
  int code = http.GET();
  if (code == 200) {
    File f = SD.open("/discs.json", FILE_WRITE);
    if (f) {
      http.writeToStream(&f);
      f.close();
      Serial.println("[SD SYNC] /discs.json atualizado do servidor com sucesso!");
    }
  }
  http.end();
}
