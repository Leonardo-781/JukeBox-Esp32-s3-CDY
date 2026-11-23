#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>
#include "track_catalog.h"
#include "ap_portal.h"

// ==============================================================================
// 1. PINAGEM OFICIAL - ESP32-S3 N16R8 (BASE DA VITROLA JUKEBOX)
// ==============================================================================

// --- Amplificador I2S MAX98357A (Audio Hi-Fi) ---
#define PIN_I2S_BCLK       15   // Bit Clock
#define PIN_I2S_LRC        16   // Left/Right Clock (WS)
#define PIN_I2S_DIN        17   // Serial Data In (DIN)

// --- Leitor RFID RC522 (Barramento SPI de Hardware) ---
#define PIN_RFID_SCK       12   // Clock SPI
#define PIN_RFID_MISO      13   // Master In Slave Out
#define PIN_RFID_MOSI      11   // Master Out Slave In
#define PIN_RFID_SDA       10   // Chip Select (SS)
#define PIN_RFID_RST_PIN   14   // Pino RST (ligado no 3.3V ou GPIO 14)

// --- Mini Ponte H (Motor DC do Prato N20) ---
#define PIN_MOTOR_IN1      4    // PWM de velocidade (~33 RPM)
#define PIN_MOTOR_IN2      5    // Sentido / Freio (LOW)

// --- Encoder Rotativo (Volume, Play/Pause e Long-Press Setup AP) ---
#define PIN_ENC_CLK        6
#define PIN_ENC_DT         7
#define PIN_ENC_SW         9

// --- Comunicacao Serial com a Tela CYD (UART1) ---
#define PIN_CYD_RX         8    // Conecta no TX da Tela CYD
#define PIN_CYD_TX         18   // Conecta no RX da Tela CYD

// ==============================================================================
// 2. CONFIGURACOES DE REDE HIBRIDA (LOCAL + NUVEM MUNDIAL)
// ==============================================================================
// Servidor Local STI (Casa)
#define LOCAL_SERVER_HOST     "192.168.0.105"
#define LOCAL_SERVER_PORT     3003
#define LOCAL_AUDIO_PORT      3004

// Servidor Global Nuvem (Acesso de Qualquer Lugar do Mundo)
#define GLOBAL_HOST           "neoface.dev.br"
#define GLOBAL_HTTPS_PORT     443
#define GLOBAL_STREAM_PATH    "/jukebox/stream"
#define GLOBAL_API_BASE       "/jukebox"

// Parametros do Motor
#define MOTOR_DEFAULT_SPEED   65
#define MOTOR_KICK_POWER      115
#define MOTOR_KICK_TIME_MS    80
#define RFID_CHECK_INTERVAL   120      // 120 ms para leitura agil e continua
#define MAX_DISC_MISSES       12       // 12 leituras de tolerancia (~1.4s) para tags em movimento no prato

// Parametros de Audio Hi-Fi com Buffer Gigante de 1 MB
#define SAMPLE_RATE           44100
#define RING_SIZE_PSRAM       1048576  // 1 MB em PSRAM (~12 segundos de som ininterrupto!)
#define RING_SIZE_SRAM        65536    // 64 KB fallback
#define PREFILL_BYTES_NET     65536    // 64 KB pre-buffer para streaming Wi-Fi/Nuvem (~740 ms)
#define PREFILL_BYTES_SD      4096     // 4 KB pre-buffer para reproducao do Cartao SD (<30 ms de latencia!)
#define PLAY_CHUNK            1024

#define AP_TIMEOUT_MS         300000   // 5 minutos de timeout no AP antes de voltar a tentar STA

// ==============================================================================
// INSTANCIAS DE HARDWARE E GERENCIADORES
// ==============================================================================
MFRC522 rfid(PIN_RFID_SDA, -1);
I2SClass i2s;
WiFiMulti* wifiMulti = nullptr;
WiFiClient lanClient;
WiFiClientSecure cloudClient;
HardwareSerial SerialCYD(1);

Preferences prefs;
WebServer apServer(80);
DNSServer dnsServer;
DynamicJsonDocument sdDiscsDoc(8192);

// Buffer circular dinamico em PSRAM
static uint8_t* audioRing = nullptr;
static uint32_t ringBufferSize = RING_SIZE_PSRAM;
static uint32_t ringBufferMask = RING_SIZE_PSRAM - 1;

static volatile uint32_t audioRingHead = 0;
static volatile uint32_t audioRingTail = 0;
static volatile bool audioBuffering    = true;

// Modo de Conexao: false = Local (Casa), true = Nuvem Global (Rua/Faculdade)
bool isCloudMode         = false;
bool isLocalAudioMode    = false;
int lanFailures          = 0;

// Modo AP Portal Cativo
bool isApMode            = false;
unsigned long apStartTime = 0;

// Estado da Vitrola
bool isDiscPresent       = false;
bool isPlaying           = false;
char activeUidHex[32]    = "NENHUM";
String currentTrackTitle = "Aproxime um vinil";
String currentArtist     = "Vitrola Jukebox";
String currentDiscName   = "Prato livre";
int progressSeconds      = 0;
int durationSeconds      = 180;
int currentVolume        = 40;
int currentMotorSpeed    = MOTOR_DEFAULT_SPEED;
int missingChecks        = 0;

// Temporizadores
unsigned long lastRfidCheckTime = 0;
unsigned long lastAudioRec      = 0;
unsigned long lastSyncTime      = 0;

// Notificacoes assincronas de disco (evita bloqueio HTTP no milissegundo de play)
static bool pendingDiscPlace    = false;
static bool pendingDiscRemove   = false;
static char pendingDiscUid[32]  = {0};

// Prototipos
void setupHardware();
void startMotorWithKick(int speed);
void stopMotor();
bool checkCardPresent(const char* targetUid);
void handleDiscPlaced(const char* uidHex);
void handleDiscRemoved();
void sendDiscEvent(const char* endpoint, const char* uidHex);
void sendPlayerAction(const char* action);
void syncServerState();
void sendCydTelemetry();
void pumpNetworkAudio();
void playTone(float freqHz, int durationMs, float amp = 0.15f);
void playNeedleDropEffect();
void playVinylStopEffect();
void reinitRfidRegisters(MFRC522::PCD_RxGain gain = MFRC522::RxGain_max);
bool maintainAudioConnection();

// Gerenciamento do Banco de Redes Wi-Fi & Portal Cativo
String getWifiDatabaseJson();
void reloadWifiMulti();
void sendWifiSyncToCyd();
void sendCydApMode(bool active);
bool addWifiToDatabase(const String& ssid, const String& pass);
bool deleteWifiFromDatabase(const String& ssid);
void startApMode();
void stopApMode();
void handleRoot();
void handleSave();
void handleDelete();
void handleExit();
void handleRedirect();
void handleApiStatus();
void handleApiTracks();
void handleApiDiscs();
void handleApiSaveDisc();
void handleApiDeleteDisc();
void handleApiTestPlay();
void handleApiSaveWifi();

// Gerenciamento de RingBuffer
static inline uint32_t audioRingUsed() {
  uint32_t h = __atomic_load_n(&audioRingHead, __ATOMIC_ACQUIRE);
  uint32_t t = __atomic_load_n(&audioRingTail, __ATOMIC_ACQUIRE);
  return h - t;
}

static inline uint32_t audioRingFree() {
  return ringBufferSize - audioRingUsed();
}

static void audioRingWrite(const uint8_t* src, uint32_t n) {
  uint32_t h = audioRingHead;
  uint32_t pos = h & ringBufferMask;
  uint32_t first = min(n, ringBufferSize - pos);
  memcpy(&audioRing[pos], src, first);
  if (n > first) {
    memcpy(&audioRing[0], src + first, n - first);
  }
  __atomic_store_n(&audioRingHead, h + n, __ATOMIC_RELEASE);
}

static void audioRingRead(uint8_t* dst, uint32_t n) {
  uint32_t t = audioRingTail;
  uint32_t pos = t & ringBufferMask;
  uint32_t first = min(n, ringBufferSize - pos);
  memcpy(dst, &audioRing[pos], first);
  if (n > first) {
    memcpy(dst + first, &audioRing[0], n - first);
  }
  __atomic_store_n(&audioRingTail, t + n, __ATOMIC_RELEASE);
}

// ==============================================================================
// BANCO DE DADOS DE REDES WI-FI PERMANENTE (NVS / PREFERENCES)
// ==============================================================================
String getWifiDatabaseJson() {
  prefs.begin("jukebox", false);
  String json = prefs.getString("wifi_db", "");
  prefs.end();

  if (json.length() == 0 || json == "[]") {
    // Insira aqui sua rede padrao ou cadastre facilmente pelo Portal Cativo AP (192.168.4.1)
    json = "[{\"s\":\"SUA_REDE_WIFI\",\"p\":\"SUA_SENHA_WIFI\"}]";
    prefs.begin("jukebox", false);
    prefs.putString("wifi_db", json);
    prefs.end();
  }
  return json;
}

void reloadWifiMulti() {
  if (wifiMulti) {
    delete wifiMulti;
  }
  wifiMulti = new WiFiMulti();

  String json = getWifiDatabaseJson();
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (!err && doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
      const char* s = obj["s"];
      const char* p = obj["p"];
      if (s && strlen(s) > 0) {
        wifiMulti->addAP(s, (p && strlen(p) > 0) ? p : "");
      }
    }
  }
}

void sendWifiSyncToCyd() {
  String json = getWifiDatabaseJson();
  StaticJsonDocument<2048> doc;
  doc["cmd"] = "SYNC_WIFI_DB";
  StaticJsonDocument<1536> dbDoc;
  deserializeJson(dbDoc, json);
  doc["db"] = dbDoc;

  String line;
  serializeJson(doc, line);
  SerialCYD.println(line);
}

void sendCydApMode(bool active) {
  StaticJsonDocument<256> doc;
  doc["cmd"]    = "WIFI_AP_MODE";
  doc["active"] = active;
  doc["ssid"]   = "Vitrola-Jukebox-Setup";
  doc["ip"]     = "192.168.4.1";

  String line;
  serializeJson(doc, line);
  SerialCYD.println(line);
}

bool addWifiToDatabase(const String& ssid, const String& pass) {
  if (ssid.length() == 0) return false;

  String json = getWifiDatabaseJson();
  StaticJsonDocument<2048> doc;
  deserializeJson(doc, json);
  JsonArray arr = doc.as<JsonArray>();

  bool found = false;
  for (JsonObject obj : arr) {
    if (obj["s"].as<String>() == ssid) {
      obj["p"] = pass;
      found = true;
      break;
    }
  }
  if (!found) {
    JsonObject newObj = arr.createNestedObject();
    newObj["s"] = ssid;
    newObj["p"] = pass;
  }

  String newJson;
  serializeJson(arr, newJson);

  prefs.begin("jukebox", false);
  prefs.putString("wifi_db", newJson);
  prefs.end();

  Serial.printf("[WI-FI DB] Rede '%s' gravada no banco permanente!\n", ssid.c_str());
  reloadWifiMulti();
  sendWifiSyncToCyd();
  return true;
}

bool deleteWifiFromDatabase(const String& ssid) {
  String json = getWifiDatabaseJson();
  StaticJsonDocument<2048> doc;
  deserializeJson(doc, json);
  JsonArray arr = doc.as<JsonArray>();

  StaticJsonDocument<2048> newDoc;
  JsonArray newArr = newDoc.to<JsonArray>();
  for (JsonObject obj : arr) {
    if (obj["s"].as<String>() != ssid) {
      newArr.add(obj);
    }
  }

  String newJson;
  serializeJson(newArr, newJson);

  prefs.begin("jukebox", false);
  prefs.putString("wifi_db", newJson);
  prefs.end();

  Serial.printf("[WI-FI DB] Rede '%s' removida do banco!\n", ssid.c_str());
  reloadWifiMulti();
  sendWifiSyncToCyd();
  return true;
}

// ==============================================================================
// PORTAL CATIVO WI-FI (MODO SETUP AP)
// ==============================================================================
void handleRedirect() {
  apServer.sendHeader("Location", "http://192.168.4.1/");
  apServer.send(302, "text/plain", "");
}

void handleExit() {
  apServer.send(200, "text/plain", "OK");
  delay(200);
  stopApMode();
}

void handleApiStatus() {
  StaticJsonDocument<256> doc;
  doc["uid"]            = activeUidHex;
  doc["is_present"]     = isDiscPresent;
  doc["is_playing"]     = isPlaying;
  doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
  doc["wifi_ssid"]      = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "";
  doc["wifi_ip"]        = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";

  String out;
  serializeJson(doc, out);
  apServer.send(200, "application/json", out);
}

void handleApiTracks() {
  String json = "[";
  for (size_t i = 0; i < TRACK_CATALOG_COUNT; i++) {
    if (i > 0) json += ",";
    json += "{\"id\":\"";
    json += TRACK_CATALOG[i].id;
    json += "\",\"n\":\"";
    String name = TRACK_CATALOG[i].name;
    name.replace("\"", "\\\"");
    json += name;
    json += "\"}";
  }
  json += "]";
  apServer.send(200, "application/json", json);
}

void handleApiDiscs() {
  String out;
  serializeJson(sdDiscsDoc, out);
  if (out.length() == 0 || out == "null") out = "{}";
  apServer.send(200, "application/json", out);
}

void handleApiSaveDisc() {
  String uid    = apServer.arg("uid");
  String name   = apServer.arg("name");
  String artist = apServer.arg("artist");
  String track  = apServer.arg("track");

  if (uid.length() == 0 || track.length() == 0) {
    apServer.send(400, "application/json", "{\"ok\":false,\"err\":\"invalid_args\"}");
    return;
  }

  // Notifica o CYD para salvar no Cartao MicroSD
  StaticJsonDocument<512> doc;
  doc["cmd"]    = "SAVE_DISC";
  doc["uid"]    = uid;
  doc["name"]   = name;
  doc["artist"] = artist;
  doc["track"]  = track;
  String line;
  serializeJson(doc, line);
  SerialCYD.println(line);

  // Atualiza cache em RAM do S3
  JsonObject obj = sdDiscsDoc.createNestedObject(uid);
  obj["name"]   = name;
  obj["artist"] = artist;
  obj["track"]  = track;
  obj["uri"]    = "spotify:track:" + track;

  // Som de confirmacao de disco gravado
  playTone(880.0f, 80, 0.20f);
  delay(90);
  playTone(1320.0f, 140, 0.20f);

  apServer.send(200, "application/json", "{\"ok\":true}");
}

void handleApiDeleteDisc() {
  String uid = apServer.arg("uid");
  if (uid.length() > 0) {
    StaticJsonDocument<256> doc;
    doc["cmd"] = "DELETE_DISC";
    doc["uid"] = uid;
    String line;
    serializeJson(doc, line);
    SerialCYD.println(line);

    sdDiscsDoc.remove(uid);
    playTone(440.0f, 100, 0.15f);
  }
  apServer.send(200, "application/json", "{\"ok\":true}");
}

void handleApiTestPlay() {
  String track = apServer.arg("track");
  if (track.length() > 0) {
    StaticJsonDocument<256> doc;
    doc["cmd"]   = "PLAY_PREVIEW";
    doc["track"] = track;
    String line;
    serializeJson(doc, line);
    SerialCYD.println(line);
  }
  apServer.send(200, "application/json", "{\"ok\":true}");
}

void handleApiSaveWifi() {
  String ssid = apServer.arg("ssid");
  String pass = apServer.arg("pass");
  ssid.trim();
  pass.trim();
  if (ssid.length() > 0) {
    addWifiToDatabase(ssid, pass);
    reloadWifiMulti();
    sendWifiSyncToCyd();
  }
  apServer.send(200, "application/json", "{\"ok\":true}");
}

void handleRoot() {
  apServer.send_P(200, "text/html", INDEX_HTML);
}

void startApMode() {
  if (isApMode) return;
  Serial.println("\n[WI-FI AP] Iniciando Modo de Ponto de Acesso e Portal de Cadastro...");
  isApMode = true;
  apStartTime = millis();

  // Solicita catalogo atualizado do cartao SD ao CYD
  SerialCYD.println("{\"cmd\":\"GET_SD_CATALOG\"}");

  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Jukebox-Setup");
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  dnsServer.start(53, "*", apIP);

  apServer.on("/", HTTP_GET, handleRoot);
  apServer.on("/api/status", HTTP_GET, handleApiStatus);
  apServer.on("/api/tracks", HTTP_GET, handleApiTracks);
  apServer.on("/api/discs", HTTP_GET, handleApiDiscs);
  apServer.on("/api/save_disc", HTTP_POST, handleApiSaveDisc);
  apServer.on("/api/delete_disc", HTTP_POST, handleApiDeleteDisc);
  apServer.on("/api/test_play", HTTP_POST, handleApiTestPlay);
  apServer.on("/api/save_wifi", HTTP_POST, handleApiSaveWifi);
  apServer.on("/exit", HTTP_GET, handleExit);

  apServer.on("/generate_204", HTTP_GET, handleRoot);
  apServer.on("/hotspot-detect.html", HTTP_GET, handleRoot);
  apServer.on("/canonical.html", HTTP_GET, handleRoot);
  apServer.on("/connecttest.txt", HTTP_GET, handleRoot);
  apServer.on("/ncsi.txt", HTTP_GET, handleRoot);
  apServer.onNotFound(handleRedirect);

  apServer.begin();

  sendCydApMode(true);
  playTone(880.0f, 120, 0.20f);
  delay(150);
  playTone(1320.0f, 180, 0.20f);
  Serial.printf("[WI-FI AP OK] Ponto de Acesso 'Jukebox-Setup' ativo em %s\n", apIP.toString().c_str());
}

void stopApMode() {
  if (!isApMode) return;
  Serial.println("[WI-FI AP] Encerrando Modo Ponto de Acesso...");
  isApMode = false;
  dnsServer.stop();
  apServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  sendCydApMode(false);
  reloadWifiMulti();
  playTone(660.0f, 100, 0.15f);
  Serial.println("[WI-FI AP OK] Retornando ao modo vitrola normal.");
}

// ==============================================================================
// SETUP PRINCIPAL (ESP32-S3 DUAL-CORE)
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n========================================================");
  Serial.println("  VITROLA JUKEBOX - S3 N16R8 ULTRA BUFFER & GLOBAL CLOUD");
  Serial.println("========================================================");
  Serial.printf("[S3 INFO] Chip: %s | Cores: %d | Flash: %u MB\n", ESP.getChipModel(), ESP.getChipCores(), ESP.getFlashChipSize() / (1024 * 1024));

  // Alocacao de 1 MB de Buffer de Audio em PSRAM (8 MB total)
  if (psramFound()) {
    Serial.printf("[PSRAM OK] %u KB de PSRAM disponivel.\n", ESP.getFreePsram() / 1024);
    ringBufferSize = RING_SIZE_PSRAM;
    ringBufferMask = RING_SIZE_PSRAM - 1;
    audioRing = (uint8_t*)ps_malloc(ringBufferSize);
    if (audioRing) {
      Serial.printf("[AUDIO BUFFER] >>> 1 MEGABYTE ALOCADO EM PSRAM (~12 segundos de som!) <<<\n");
    }
  }

  if (!audioRing) {
    ringBufferSize = RING_SIZE_SRAM;
    ringBufferMask = RING_SIZE_SRAM - 1;
    audioRing = (uint8_t*)malloc(ringBufferSize);
    Serial.printf("[AUDIO BUFFER] Fallback: 64 KB alocado na SRAM interna.\n");
  }

  // Inicializa UART para comunicacao com a Tela CYD em Alta Velocidade (1.5 Mbps)
  SerialCYD.begin(1500000, SERIAL_8N1, PIN_CYD_RX, PIN_CYD_TX);
  Serial.println("[CYD UART] Comunicacao com a tela pronta (1.5M baud, RX: 8, TX: 18).");

  // Pino RST do RFID
  pinMode(PIN_RFID_RST_PIN, OUTPUT);
  digitalWrite(PIN_RFID_RST_PIN, HIGH);

  // Inicializa Motores, RFID e I2S
  setupHardware();

  // Cliente Seguro TLS (sem validacao rigida de certificado para velocidade)
  cloudClient.setInsecure();

  // 1. Carrega Banco de Dados Permanente de Redes Wi-Fi
  reloadWifiMulti();
  Serial.println("[REDE] Verificando redes salvas...");

  int tries = 0;
  while (wifiMulti->run() != WL_CONNECTED && tries < 6) {
    delay(250);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[REDE OK] Conectado a %s! IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    playTone(1046.5f, 150, 0.20f);

    StaticJsonDocument<256> doc;
    doc["cmd"]  = "WIFI_CONNECTED";
    doc["ssid"] = WiFi.SSID();
    doc["ip"]   = WiFi.localIP().toString();
    String line;
    serializeJson(doc, line);
    SerialCYD.println(line);

    // Envia banco sincronizado para a CYD
    sendWifiSyncToCyd();
  } else {
    Serial.println("\n[MODO OFFLINE] Nenhuma rede Wi-Fi conectada. Vitrola 100% pronta com Cartao MicroSD!");
    WiFi.mode(WIFI_STA);
    playTone(880.0f, 100, 0.15f);
  }
}

// ==============================================================================
// LOOP PRINCIPAL
// ==============================================================================
void loop() {
  unsigned long now = millis();

  // 0a. Leitura do Giro do Encoder (Controle de Volume Suave)
  static int lastEncClk = HIGH;
  static unsigned long lastEncTurnTime = 0;
  int currentEncClk = digitalRead(PIN_ENC_CLK);
  if (currentEncClk != lastEncClk && currentEncClk == LOW) {
    if (now - lastEncTurnTime > 15) {
      lastEncTurnTime = now;
      if (digitalRead(PIN_ENC_DT) != currentEncClk) {
        currentVolume = min(100, currentVolume + 5);
      } else {
        currentVolume = max(0, currentVolume - 5);
      }
      Serial.printf("[ENCODER VOLUME] %d%%\n", currentVolume);
      sendCydTelemetry();
      playTone(1100.0f + (currentVolume * 6.0f), 15, 0.08f); // Feedback sonoro sutil
    }
  }
  lastEncClk = currentEncClk;

  // 0b. Leitura do Botao do Encoder (Curto = Play/Pause | Longo 3s = Setup AP)
  static unsigned long encPressStart = 0;
  static bool encPressed = false;
  int encState = digitalRead(PIN_ENC_SW);
  if (encState == LOW) {
    if (!encPressed) {
      encPressed = true;
      encPressStart = now;
    } else if ((now - encPressStart >= 3000) && !isApMode) {
      Serial.println("[ENCODER] Botao segurado por 3s! Ativando Modo Setup Wi-Fi...");
      startApMode();
      encPressed = false;
    }
  } else {
    if (encPressed) {
      unsigned long duration = now - encPressStart;
      if (duration >= 40 && duration < 800 && !isApMode) {
        // Clique curto: Alterna Play / Pause!
        isPlaying = !isPlaying;
        Serial.printf("[ENCODER CLICK] Play/Pause alternado! isPlaying = %s\n", isPlaying ? "SIM" : "NAO");
        if (isPlaying) {
          if (isDiscPresent) startMotorWithKick(currentMotorSpeed > 0 ? currentMotorSpeed : MOTOR_DEFAULT_SPEED);
          sendPlayerAction("toggle");
          playTone(880.0f, 60, 0.15f);
        } else {
          stopMotor();
          sendPlayerAction("toggle");
          playTone(440.0f, 80, 0.15f);
        }
        sendCydTelemetry();
      }
      encPressed = false;
    }
  }

  // 0c. Leitura SerialCYD (Comandos JSON ou Audio Blocks do Cartao SD)
  while (SerialCYD.available() > 0) {
    int peekByte = SerialCYD.peek();
    if (peekByte == '{') {
      String line = SerialCYD.readStringUntil('\n');
      line.trim();
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, line) == DeserializationError::Ok) {
        if (doc.containsKey("cmd")) {
          String c = doc["cmd"].as<String>();
          if (c == "LOCAL_AUDIO_START") {
            Serial.println("[AUDIO FONTE] >> MODO LOCAL ATIVADO: Reproduzindo do Cartao SD do CYD <<");
            isLocalAudioMode = true;
            if (lanClient.connected()) lanClient.stop();
            if (cloudClient.connected()) cloudClient.stop();
          } else if (c == "LOCAL_AUDIO_END") {
            Serial.println("[AUDIO FONTE] >> Fim da faixa do Cartao SD <<");
            isLocalAudioMode = false;
          } else if (c == "SD_DISCS") {
            if (doc.containsKey("data")) {
              sdDiscsDoc.clear();
              sdDiscsDoc.set(doc["data"]);
              Serial.println("[S3] Catalogo de discos sincronizado do MicroSD!");
            }
          } else if (c == "AP_REQUEST") {
            Serial.println("[S3] Requisicao de Modo Setup AP recebida do CYD (Touch)");
            startApMode();
          } else if (c == "AP_EXIT") {
            Serial.println("[S3] Requisicao de saida de Modo AP recebida do CYD");
            stopApMode();
          } else if (c == "PLAYER_TOGGLE") {
            isPlaying = !isPlaying;
            if (isPlaying) {
              if (isDiscPresent) startMotorWithKick(currentMotorSpeed > 0 ? currentMotorSpeed : MOTOR_DEFAULT_SPEED);
            } else {
              stopMotor();
            }
            sendPlayerAction("toggle");
            sendCydTelemetry();
          } else if (c == "PLAYER_NEXT") {
            sendPlayerAction("next");
          } else if (c == "PLAYER_PREV") {
            sendPlayerAction("prev");
          }
        }
      }
    } else if (peekByte == 0xAA) {
      if (SerialCYD.available() >= 4) {
        uint8_t hdr[4];
        SerialCYD.readBytes((char*)hdr, 4);
        if (hdr[0] == 0xAA && hdr[1] == 0x55) {
          uint16_t len = ((uint16_t)hdr[2] << 8) | hdr[3];
          if (len > 0 && len <= 512) {
            uint8_t audioBuf[512];
            int r = SerialCYD.readBytes((char*)audioBuf, len);
            if (r > 0) {
              audioRingWrite(audioBuf, r);
            }
          }
        }
      } else {
        break; // Aguarda os 4 bytes do header chegarem
      }
    } else {
      // Descarta byte invalido / quebra de linha solta
      SerialCYD.read();
    }
  }

  // Se estiver no Modo AP de Configuracao, atende clientes web e leitor RFID
  if (isApMode) {
    dnsServer.processNextRequest();
    apServer.handleClient();

    // Verificacao RFID no prato em tempo real para cadastro no portal!
    if (now - lastRfidCheckTime >= 150) {
      lastRfidCheckTime = now;
      if (isDiscPresent) {
        if (!checkCardPresent(activeUidHex)) {
          missingChecks++;
          if (missingChecks >= 3) {
            byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
            if (v == 0x00 || v == 0xFF) {
              rfid.PCD_Init();
              reinitRfidRegisters(MFRC522::RxGain_max);
            }
          }
          if (missingChecks >= 6) {
            isDiscPresent = false;
            strncpy(activeUidHex, "NENHUM", sizeof(activeUidHex) - 1);
            SerialCYD.println("{\"cmd\":\"AP_TAG_UPDATE\",\"uid\":\"\"}");
          }
        } else {
          missingChecks = 0;
        }
      } else {
        byte bufferATQA[2];
        byte bufferSize = sizeof(bufferATQA);
        byte st = rfid.PICC_WakeupA(bufferATQA, &bufferSize);
        if (st != MFRC522::STATUS_OK && st != MFRC522::STATUS_COLLISION) {
          bufferSize = sizeof(bufferATQA);
          st = rfid.PICC_RequestA(bufferATQA, &bufferSize);
        }
        if (st == MFRC522::STATUS_OK || st == MFRC522::STATUS_COLLISION) {
          if (rfid.PICC_ReadCardSerial()) {
            char uidHex[32] = {0};
            for (byte i = 0; i < rfid.uid.size; i++) {
              snprintf(uidHex + strlen(uidHex), sizeof(uidHex) - strlen(uidHex), "%02X", rfid.uid.uidByte[i]);
            }
            rfid.PCD_StopCrypto1();
            if (strcmp(activeUidHex, uidHex) != 0) {
              strncpy(activeUidHex, uidHex, sizeof(activeUidHex) - 1);
              isDiscPresent = true;
              missingChecks = 0;
              Serial.printf("[AP RFID] Tag detectada no prato: %s\n", uidHex);
              SerialCYD.printf("{\"cmd\":\"AP_TAG_UPDATE\",\"uid\":\"%s\"}\n", uidHex);
              playTone(1320.0f, 40, 0.12f);
            }
          }
        }
      }
    }

    // Playback de audio de teste/preview se disparado no portal
    uint32_t used = audioRingUsed();
    if (used > 0) {
      alignas(4) static int16_t monoIn[PLAY_CHUNK / 2];
      alignas(4) static int16_t stereoOut[PLAY_CHUNK];
      uint32_t n = min(used, (uint32_t)PLAY_CHUNK) & ~1u;
      if (n > 0) {
        audioRingRead((uint8_t*)monoIn, n);
        int count = n / 2;
        int vol = currentVolume;
        for (int i = 0; i < count; i++) {
          int16_t sample = (vol < 100) ? (int16_t)(((int32_t)monoIn[i] * vol) / 100) : monoIn[i];
          stereoOut[i * 2]     = sample;
          stereoOut[i * 2 + 1] = sample;
        }
        i2s.write((const uint8_t*)stereoOut, count * 4);
      }
    }

    if (now - apStartTime > AP_TIMEOUT_MS) {
      Serial.println("[WI-FI AP] Timeout de inatividade. Encerrando AP...");
      stopApMode();
    }
    delay(2);
    return;
  }

  // 1. Manutencao de Rede Wi-Fi e Stream de Audio (apenas se nao estiver tocando local do SD)
  if (!isLocalAudioMode && WiFi.status() == WL_CONNECTED) {
    if (maintainAudioConnection()) {
      pumpNetworkAudio();
    }
  } else {
    // Tenta reconectar periodicamente
    static unsigned long lastReconnectTry = 0;
    if (now - lastReconnectTry >= 4000) {
      lastReconnectTry = now;
      wifiMulti->run();
    }
  }

  // 2. Playback I2S continuo com o Buffer Gigante
  uint32_t used = audioRingUsed();
  uint32_t targetPrefill = isLocalAudioMode ? PREFILL_BYTES_SD : PREFILL_BYTES_NET;
  if (audioBuffering) {
    if (used >= targetPrefill) audioBuffering = false;
  } else if (used < 2) {
    audioBuffering = true;
  }

  if (!audioBuffering && isPlaying) {
    alignas(4) static int16_t monoIn[PLAY_CHUNK / 2];
    alignas(4) static int16_t stereoOut[PLAY_CHUNK];
    uint32_t n = min(used, (uint32_t)PLAY_CHUNK) & ~1u;
    if (n > 0) {
      audioRingRead((uint8_t*)monoIn, n);
      int count = n / 2;
      int vol = currentVolume;
      for (int i = 0; i < count; i++) {
        int16_t sample = (vol < 100) ? (int16_t)(((int32_t)monoIn[i] * vol) / 100) : monoIn[i];
        stereoOut[i * 2]     = sample;
        stereoOut[i * 2 + 1] = sample;
      }
      i2s.write((const uint8_t*)stereoOut, count * 4);
    }
  }

  // 3. Verificacao do Leitor RFID no Prato
  if (now - lastRfidCheckTime >= RFID_CHECK_INTERVAL) {
    lastRfidCheckTime = now;

    if (isDiscPresent) {
      bool present = checkCardPresent(activeUidHex);
      if (present) {
        missingChecks = 0;
      } else {
        missingChecks++;
        if (missingChecks >= 3) {
          // Watchdog RC522: se o barramento SPI travar por interferencia do motor, recupera limpo
          byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
          if (v == 0x00 || v == 0xFF) {
            rfid.PCD_Init();
            reinitRfidRegisters(MFRC522::RxGain_max);
          }
        }
        if (missingChecks >= MAX_DISC_MISSES) {
          Serial.println("[RFID] Disco retirado da vitrola!");
          handleDiscRemoved();
        }
      }
    } else {
      byte bufferATQA[2];
      byte bufferSize = sizeof(bufferATQA);

      // Leitura de novo disco: aceita STATUS_OK e STATUS_COLLISION (normal em tags em movimento)
      byte st = rfid.PICC_WakeupA(bufferATQA, &bufferSize);
      if (st != MFRC522::STATUS_OK && st != MFRC522::STATUS_COLLISION) {
        bufferSize = sizeof(bufferATQA);
        st = rfid.PICC_RequestA(bufferATQA, &bufferSize);
      }

      if (st == MFRC522::STATUS_OK || st == MFRC522::STATUS_COLLISION) {
        if (rfid.PICC_ReadCardSerial()) {
          char uidHex[32] = {0};
          for (byte i = 0; i < rfid.uid.size; i++) {
            snprintf(uidHex + strlen(uidHex), sizeof(uidHex) - strlen(uidHex), "%02X", rfid.uid.uidByte[i]);
          }
          rfid.PCD_StopCrypto1();

          Serial.printf("[RFID] >>> DISCO DETECTADO! UID: %s <<<\n", uidHex);
          handleDiscPlaced(uidHex);
        }
      }
    }
  }

  // 4. Envio de eventos pendentes de disco em segundo plano (evita congelar CPU no inicio do play)
  if (pendingDiscPlace) {
    pendingDiscPlace = false;
    sendDiscEvent("/api/disc/place", pendingDiscUid);
  } else if (pendingDiscRemove) {
    pendingDiscRemove = false;
    sendDiscEvent("/api/disc/remove", "");
  }

  // 5. Sincronizacao de Estado com o Servidor (a cada 3s)
  if (now - lastSyncTime >= 3000) {
    lastSyncTime = now;
    syncServerState();
    sendCydTelemetry();
  }
}

// ==============================================================================
// GESTAO DA CONEXAO DE STREAMING (LAN DIRETA VS NUVEM HTTPS)
// ==============================================================================
bool maintainAudioConnection() {
  if (isLocalAudioMode) return false;

  if (isCloudMode) {
    if (cloudClient.connected()) return true;
  } else {
    if (lanClient.connected()) return true;
  }

  if (!isPlaying) return false;

  unsigned long now = millis();
  if (now - lastAudioRec < 1500) return false;
  lastAudioRec = now;

  // 1. Tenta Conexao Local (Casa - Rapida e Direta)
  if (!isCloudMode) {
    Serial.printf("[AUDIO LAN] Tentando servidor local %s:%d...\n", LOCAL_SERVER_HOST, LOCAL_AUDIO_PORT);
    if (lanClient.connect(LOCAL_SERVER_HOST, LOCAL_AUDIO_PORT, 800)) {
      Serial.println("[AUDIO LAN OK] Conectado na rede local!");
      lanFailures = 0;
      return true;
    } else {
      lanFailures++;
      Serial.printf("[AUDIO LAN AVISO] Falha ao conectar local (%d/2).\n", lanFailures);
      if (lanFailures >= 2) {
        Serial.println("[AUDIO ROTEAMENTO] Chaveando para modo NUVEM GLOBAL (neoface.dev.br)!");
        isCloudMode = true;
      }
    }
  }

  // 2. Modo Nuvem Global (Fora de Casa / Faculdade / 4G)
  if (isCloudMode) {
    Serial.printf("[AUDIO CLOUD] Conectando a https://%s%s...\n", GLOBAL_HOST, GLOBAL_STREAM_PATH);
    if (cloudClient.connect(GLOBAL_HOST, GLOBAL_HTTPS_PORT, 3000)) {
      String getReq = String("GET ") + GLOBAL_STREAM_PATH + " HTTP/1.1\r\n" +
                      "Host: " + GLOBAL_HOST + "\r\n" +
                      "User-Agent: Jukebox-S3-HiFi/2.0\r\n" +
                      "Accept: */*\r\n" +
                      "Connection: keep-alive\r\n\r\n";
      cloudClient.print(getReq);
      Serial.println("[AUDIO CLOUD OK] Stream HTTPS Mundial Conectado com Sucesso!");
      return true;
    } else {
      Serial.println("[AUDIO CLOUD ERRO] Falha ao conectar na nuvem. Tentando novamente.");
      lanFailures = 0;
      isCloudMode = false;
    }
  }

  return false;
}

// Drena dados de audio da rede para o buffer da PSRAM
void pumpNetworkAudio() {
  Stream* stream = isCloudMode ? (Stream*)&cloudClient : (Stream*)&lanClient;
  if (!stream) return;

  static uint8_t tmp[2048];
  for (int i = 0; i < 16; i++) {
    int avail = stream->available();
    uint32_t space = audioRingFree();
    if (avail <= 0 || space == 0) break;
    int toRead = min((uint32_t)avail, min(space, (uint32_t)sizeof(tmp)));
    int r = stream->readBytes(tmp, toRead);
    if (r <= 0) break;
    audioRingWrite(tmp, r);
  }
}

// ==============================================================================
// INICIALIZACAO DE HARDWARE
// ==============================================================================
void reinitRfidRegisters(MFRC522::PCD_RxGain gain) {
  digitalWrite(PIN_RFID_SDA, HIGH);
  rfid.PCD_WriteRegister(MFRC522::TModeReg, 0x80);
  rfid.PCD_WriteRegister(MFRC522::TPrescalerReg, 0xA9);
  rfid.PCD_WriteRegister(MFRC522::TReloadRegH, 0x03);
  rfid.PCD_WriteRegister(MFRC522::TReloadRegL, 0xE8);
  rfid.PCD_WriteRegister(MFRC522::TxASKReg, 0x40);
  rfid.PCD_WriteRegister(MFRC522::ModeReg, 0x3D);
  rfid.PCD_WriteRegister(MFRC522::TxModeReg, 0x00);
  rfid.PCD_WriteRegister(MFRC522::RxModeReg, 0x00);
  rfid.PCD_WriteRegister(MFRC522::ModWidthReg, 0x26);
  rfid.PCD_SetAntennaGain(gain);
  rfid.PCD_AntennaOn();
}

void setupHardware() {
  pinMode(PIN_MOTOR_IN1, OUTPUT);
  pinMode(PIN_MOTOR_IN2, OUTPUT);
  stopMotor();

  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);

  // Motor Kickstart Teste inicial
  startMotorWithKick(MOTOR_DEFAULT_SPEED);
  delay(1000);
  stopMotor();
  Serial.println("[MOTOR TEST] Motor pronto.");

  // SPI Hardware
  SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SDA);
  pinMode(PIN_RFID_SDA, OUTPUT);
  digitalWrite(PIN_RFID_SDA, HIGH);

  rfid.PCD_Init();
  delay(50);
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("[RFID] Registrador de Versao RC522: 0x%02X\n", v);
  if (v == 0x91 || v == 0x92 || v == 0x82 || v == 0x12) {
    Serial.println("[RFID OK] Modulo RC522 pronto (Versao 0x82)!");
    reinitRfidRegisters(MFRC522::RxGain_max);
  } else {
    Serial.println("[RFID ALERTA] RC522 nao respondeu como esperado. Verifique a pinagem.");
  }

  // I2S MAX98357A
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN, -1, -1);
  if (i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[OK] I2S MAX98357A ativo no ESP32-S3.");
  } else {
    Serial.println("[FALHA] Nao foi possivel iniciar I2S!");
  }
}

void startMotorWithKick(int speed) {
  speed = constrain(speed, 0, 255);
  digitalWrite(PIN_MOTOR_IN2, LOW);
  analogWrite(PIN_MOTOR_IN1, MOTOR_KICK_POWER);
  delay(MOTOR_KICK_TIME_MS);
  analogWrite(PIN_MOTOR_IN1, speed);
  currentMotorSpeed = speed;
}

void stopMotor() {
  analogWrite(PIN_MOTOR_IN1, 0);
  digitalWrite(PIN_MOTOR_IN2, LOW);
  currentMotorSpeed = 0;
}

bool checkCardPresent(const char* targetUid) {
  byte bufferATQA[2];
  byte bufferSize = sizeof(bufferATQA);

  // Testa deteccao da tag no campo RF: aceita STATUS_OK e STATUS_COLLISION
  byte st = rfid.PICC_WakeupA(bufferATQA, &bufferSize);
  if (st != MFRC522::STATUS_OK && st != MFRC522::STATUS_COLLISION) {
    bufferSize = sizeof(bufferATQA);
    st = rfid.PICC_RequestA(bufferATQA, &bufferSize);
    if (st != MFRC522::STATUS_OK && st != MFRC522::STATUS_COLLISION) {
      return false;
    }
  }

  if (!rfid.PICC_ReadCardSerial()) return false;

  char uid[32] = {0};
  for (byte i = 0; i < rfid.uid.size; i++) {
    snprintf(uid + strlen(uid), sizeof(uid) - strlen(uid), "%02X", rfid.uid.uidByte[i]);
  }

  // NAO adormecemos a tag com PICC_HaltA() para manter a comunicacao estavel durante a rotacao
  rfid.PCD_StopCrypto1();

  return (strcasecmp(uid, targetUid) == 0);
}

// ==============================================================================
// EFEITOS SONOROS VINTAGE (NEEDLE DROP & VINYL BRAKE)
// ==============================================================================
void playNeedleDropEffect() {
  Serial.println("[AUDIO FX] >>> Reproduzindo Efeito Agulha no Vinil (Needle Drop) <<<");
  // 1. Thump grave sutil quando o braco pousa no disco
  playTone(85.0f, 60, 0.35f);
  delay(20);

  // 2. Chiado e estalos de vinil
  const int crackleSamples = 128;
  alignas(4) int16_t noiseBuf[crackleSamples * 2];
  for (int iter = 0; iter < 10; iter++) {
    for (int i = 0; i < crackleSamples; i++) {
      int16_t randVal = (int16_t)(random(-3000, 3000));
      if (random(0, 100) > 95) randVal = (int16_t)(random(-14000, 14000));
      int16_t sample = (int16_t)(((int32_t)randVal * currentVolume) / 100);
      noiseBuf[i * 2]     = sample;
      noiseBuf[i * 2 + 1] = sample;
    }
    i2s.write((const uint8_t*)noiseBuf, sizeof(noiseBuf));
    delay(10);
  }
}

void playVinylStopEffect() {
  Serial.println("[AUDIO FX] >>> Desaceleracao suave do prato de vinil (Brake) <<<");
  for (float f = 380.0f; f > 70.0f; f -= 22.0f) {
    playTone(f, 30, 0.20f * (f / 380.0f));
  }
}

// ==============================================================================
// DISCO COLOCADO / RETIRADO
// ==============================================================================
void handleDiscPlaced(const char* uidHex) {
  strncpy(activeUidHex, uidHex, sizeof(activeUidHex) - 1);
  isDiscPresent = true;
  isPlaying = true;
  isLocalAudioMode = true;
  missingChecks = 0;

  // 1. NOTIFICACAO INSTANTANEA AO CYD (0 ms! Dispara busca e streaming local no SD imediatamente)
  sendCydTelemetry();

  // 2. Aciona o motor com impulso de partida (Kickstart)
  startMotorWithKick(currentMotorSpeed > 0 ? currentMotorSpeed : MOTOR_DEFAULT_SPEED);

  // 3. Efeito vintage de agulha no sulco
  playNeedleDropEffect();

  // 4. Agenda envio HTTP para o servidor sem congelar o inicio da musica (se conectado ao Wi-Fi)
  if (WiFi.status() == WL_CONNECTED) {
    strncpy(pendingDiscUid, uidHex, sizeof(pendingDiscUid) - 1);
    pendingDiscPlace = true;
    pendingDiscRemove = false;
  }
}

void handleDiscRemoved() {
  isDiscPresent = false;
  isPlaying = false;
  isLocalAudioMode = false;
  strncpy(activeUidHex, "NENHUM", sizeof(activeUidHex) - 1);

  // 1. Notifica o CYD imediatamente para cessar o streaming local
  sendCydTelemetry();

  // 2. Efeito sonoro de parada e desligamento do motor
  playVinylStopEffect();
  stopMotor();

  // 3. Agenda envio de remocao ao servidor (se conectado ao Wi-Fi)
  if (WiFi.status() == WL_CONNECTED) {
    pendingDiscRemove = true;
    pendingDiscPlace = false;
  }
}

// Envia evento para a API (com suporte hibrido LAN vs Nuvem)
void sendDiscEvent(const char* endpoint, const char* uidHex) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url;
  if (isCloudMode) {
    url = "https://" + String(GLOBAL_HOST) + String(GLOBAL_API_BASE) + String(endpoint);
    http.begin(url);
  } else {
    url = "http://" + String(LOCAL_SERVER_HOST) + ":" + String(LOCAL_SERVER_PORT) + String(endpoint);
    http.begin(url);
  }
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = (strlen(uidHex) > 0) ? "{\"uid\":\"" + String(uidHex) + "\"}" : "{}";
  int code = http.POST(jsonPayload);
  if (code > 0) {
    Serial.printf("[POST %s] Resposta: %d\n", endpoint, code);
  }
  http.end();
}

void sendPlayerAction(const char* action) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url;
  if (isCloudMode) {
    url = "https://" + String(GLOBAL_HOST) + String(GLOBAL_API_BASE) + "/api/player/" + String(action);
    http.begin(url);
  } else {
    url = "http://" + String(LOCAL_SERVER_HOST) + ":" + String(LOCAL_SERVER_PORT) + "/api/player/" + String(action);
    http.begin(url);
  }
  http.setTimeout(1200);
  http.POST("{}");
  http.end();
}

// Sincroniza estado com o Servidor (com suporte hibrido)
void syncServerState() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url;
  if (isCloudMode) {
    url = "https://" + String(GLOBAL_HOST) + String(GLOBAL_API_BASE) + "/api/status";
    http.begin(url);
  } else {
    url = "http://" + String(LOCAL_SERVER_HOST) + ":" + String(LOCAL_SERVER_PORT) + "/api/status";
    http.begin(url);
  }
  http.setTimeout(1500);

  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      currentTrackTitle = doc["title"] | currentTrackTitle;
      currentArtist     = doc["artist"] | currentArtist;
      currentDiscName   = doc["disc_name"] | currentDiscName;
      progressSeconds   = doc["progress_sec"] | 0;
      durationSeconds   = doc["duration_sec"] | 180;
    }
  }
  http.end();
}

// Envia telemetria JSON via Serial para a tela CYD atualizar
void sendCydTelemetry() {
  StaticJsonDocument<256> doc;
  doc["title"]   = currentTrackTitle;
  doc["artist"]  = currentArtist;
  doc["disc"]    = currentDiscName;
  doc["uid"]     = activeUidHex;
  doc["playing"] = isPlaying;
  doc["present"] = isDiscPresent;
  doc["vol"]     = currentVolume;

  String line;
  serializeJson(doc, line);
  SerialCYD.println(line);
}

void playTone(float freqHz, int durationMs, float amp) {
  const int chunkSamples = 128;
  alignas(4) int16_t buf[chunkSamples * 2];
  int totalSamples = (SAMPLE_RATE * durationMs) / 1000;
  float gain = (currentVolume / 100.0f) * amp * 32000.0f;
  double phase = 0.0, inc = (2.0 * M_PI * freqHz) / (double)SAMPLE_RATE;
  int idx = 0;
  for (int i = 0; i < totalSamples; i++) {
    int16_t sample = (int16_t)(sinf((float)phase) * gain);
    buf[idx++] = sample;
    buf[idx++] = sample;
    if (idx >= chunkSamples * 2) {
      i2s.write((const uint8_t*)buf, idx * 2);
      idx = 0;
    }
    phase += inc;
    if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
  }
  if (idx > 0) {
    i2s.write((const uint8_t*)buf, idx * 2);
  }
}
