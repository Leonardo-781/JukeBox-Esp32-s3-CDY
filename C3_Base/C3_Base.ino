#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>

// ==============================================================================
// 1. PINAGEM OFICIAL - ESP32-C3 SUPER MINI (BASE DA VITROLA)
// ==============================================================================

// --- Leitor RFID RC522 (Barramento SPI de Hardware) ---
#define PIN_RFID_SCK       4
#define PIN_RFID_MISO      5
#define PIN_RFID_MOSI      6
#define PIN_RFID_SDA       7    // Chip Select do RFID
#define PIN_RFID_RST       -1   // Ligado direto no 3.3V

// --- Mini Ponte H (Motor DC do Prato N20) ---
#define PIN_MOTOR_IN1      0    // PWM velocidade (~33 RPM)
#define PIN_MOTOR_IN2      1    // Sentido / Freio (LOW)

// --- Amplificador I2S MAX98357A (Som Hi-Fi na Base) ---
#define PIN_I2S_BCLK       2    // Bit Clock
#define PIN_I2S_LRC        3    // Left/Right Clock (WS)
#define PIN_I2S_DIN        10   // Serial Data In (DIN)

// --- Comunicacao Serial com a Tela CYD (UART) ---
#define PIN_CYD_TX         21   // TX do C3 -> Conecta no RX da CYD
#define PIN_CYD_RX         20   // RX do C3 -> Conecta no TX da CYD

// ==============================================================================
// 2. CONFIGURACOES DE REDE E SERVIDOR STI SPOTIFY
// ==============================================================================
#define WIFI_SSID          "SUA_REDE_WIFI"
#define WIFI_PASS          "SUA_SENHA_WIFI"
#define SERVER_HOST        "192.168.0.105"
#define SERVER_PORT        3003
#define AUDIO_PORT         3004

#define MOTOR_DEFAULT_SPEED   65
#define MOTOR_KICK_POWER      115
#define MOTOR_KICK_TIME_MS    80
#define RFID_CHECK_INTERVAL   350
#define MAX_DISC_MISSES       4

#define SAMPLE_RATE           44100
#define AUDIO_RING_SIZE       65536   // 64 KB buffer circular no C3 (~740ms de som)
#define AUDIO_RING_MASK       (AUDIO_RING_SIZE - 1)
#define AUDIO_PREFILL_BYTES   24576   // 24 KB pre-buffer (~278ms contra oscilacoes de Wi-Fi)
#define AUDIO_PLAY_CHUNK      512

// ==============================================================================
// INSTANCIAS DE HARDWARE
// ==============================================================================
MFRC522 rfid(PIN_RFID_SDA, PIN_RFID_RST);
I2SClass i2s;
WiFiClient audioClient;

// Buffer de Audio RingBuffer
alignas(4) static uint8_t audioRing[AUDIO_RING_SIZE];
static volatile uint32_t audioRingHead = 0;
static volatile uint32_t audioRingTail = 0;
static volatile bool audioBuffering    = true;

// Estado
bool isDiscPresent       = false;
bool isPlaying           = false;
char activeUidHex[16]    = "NENHUM";
String currentTrackTitle = "Aproxime um vinil";
String currentArtist     = "Vitrola Jukebox";
String currentDiscName   = "Prato livre";
int progressSeconds      = 0;
int durationSeconds      = 180;
int currentVolume        = 40;
uint8_t missingChecks    = 0;

unsigned long lastRfidCheckTime = 0;
unsigned long lastStatusPoll    = 0;
unsigned long lastAudioRec      = 0;

// ==============================================================================
// PROTOTIPOS
// ==============================================================================
void setupHardware();
void connectWifi();
void startMotor(int speed);
void stopMotor();
bool checkCardPresent(const char* targetUid);
void handleDiscPlaced(const char* uidHex);
void handleDiscRemoved();
void sendStatusToCYD();
void pollServerStatus();
void pumpNetworkAudio();
void playTone(float freqHz, int durationMs, float amp = 0.15f);
void reinitRfidRegisters(MFRC522::PCD_RxGain gain = MFRC522::RxGain_max);

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
  uint32_t first = min(n, AUDIO_RING_SIZE - pos);
  memcpy(&audioRing[pos], src, first);
  if (n > first) {
    memcpy(&audioRing[0], src + first, n - first);
  }
  __atomic_store_n(&audioRingHead, h + n, __ATOMIC_RELEASE);
}

static void audioRingRead(uint8_t* dst, uint32_t n) {
  uint32_t t = audioRingTail;
  uint32_t pos = t & AUDIO_RING_MASK;
  uint32_t first = min(n, AUDIO_RING_SIZE - pos);
  memcpy(dst, &audioRing[pos], first);
  if (n > first) {
    memcpy(dst + first, &audioRing[0], n - first);
  }
  __atomic_store_n(&audioRingTail, t + n, __ATOMIC_RELEASE);
}

// ==============================================================================
// SETUP DO ESP32-C3
// ==============================================================================
void setup() {
  // Inicializa UART USB CDC (PC) e UART0 fisica (comunicacao com a tela CYD)
  Serial.begin(115200);
  Serial0.begin(115200, SERIAL_8N1, PIN_CYD_RX, PIN_CYD_TX);
  delay(300);

  Serial.println("\n========================================================");
  Serial.println("  BASE DA VITROLA - ESP32-C3 SUPER MINI (MOTOR + RFID + I2S)");
  Serial.println("========================================================");

  setupHardware();
  connectWifi();

  // Som suave de confirmacao no MAX98357A
  playTone(523.25f, 100, 0.10f);
  playTone(659.25f, 100, 0.10f);
  playTone(783.99f, 150, 0.12f);

  sendStatusToCYD();
  Serial.println("[BASE] Pronta! Aguardando disco no prato...");
}

// ==============================================================================
// LOOP DO ESP32-C3
// ==============================================================================
void loop() {
  unsigned long now = millis();

  // 0. Comandos Interativos de Bancada via Serial USB (COM16)
  while (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "MOTOR:ON") {
      startMotor(MOTOR_DEFAULT_SPEED);
      Serial.println("[MANUAL] Motor ligado!");
    } else if (cmd == "MOTOR:OFF") {
      stopMotor();
      Serial.println("[MANUAL] Motor desligado!");
    } else if (cmd.startsWith("TAG:")) {
      String fakeUid = cmd.substring(4);
      Serial.printf("[MANUAL] Simulando insercao da tag: %s\n", fakeUid.c_str());
      handleDiscPlaced(fakeUid.c_str());
    } else if (cmd == "TAG:EJECT") {
      Serial.println("[MANUAL] Simulando retirada do vinil.");
      handleDiscRemoved();
    } else if (cmd == "TONE") {
      playTone(523.25f, 150, 0.15f);
      playTone(659.25f, 150, 0.15f);
      playTone(783.99f, 200, 0.18f);
      Serial.println("[MANUAL] Som de teste reproduzido!");
    } else if (cmd == "STATUS") {
      byte ver = rfid.PCD_ReadRegister(MFRC522::VersionReg);
      Serial.printf("[STATUS] IP: %s | WiFi: %d | I2S: OK | RFID Version: 0x%02X | Motor: %s\n",
                    WiFi.localIP().toString().c_str(), WiFi.status() == WL_CONNECTED, ver,
                    isDiscPresent ? "GIRANDO" : "PARADO");
    }
  }

  // Heartbeat do RFID a cada 2.5 segundos se prato vazio
  static unsigned long lastRfidLog = 0;
  if (!isDiscPresent && now - lastRfidLog >= 2500) {
    lastRfidLog = now;
    byte ver = rfid.PCD_ReadRegister(MFRC522::VersionReg);
    byte tx = rfid.PCD_ReadRegister(MFRC522::TxControlReg);
    Serial.printf("[RFID] Version: 0x%02X | TxControl: 0x%02X (Antena %s) | Aguardando tag...\n",
                  ver, tx, (tx & 0x03) == 0x03 ? "LIGADA" : "DESLIGADA");
  }

  // 1. Manutencao do Stream TCP de Audio (Porta 3004)
  if (WiFi.status() == WL_CONNECTED) {
    if (!audioClient.connected()) {
      if (now - lastAudioRec >= 3000) {
        lastAudioRec = now;
        Serial.printf("[AUDIO] Conectando ponte TCP %s:%d...\n", SERVER_HOST, AUDIO_PORT);
        if (audioClient.connect(SERVER_HOST, AUDIO_PORT, 2000)) {
          audioClient.setNoDelay(true);
          Serial.println("[AUDIO] Stream TCP conectado!");
        }
      }
    } else {
      pumpNetworkAudio();
    }
  }

  // 2. Playback I2S continuo
  uint32_t used = audioRingUsed();
  if (audioBuffering) {
    if (used >= AUDIO_PREFILL_BYTES) audioBuffering = false;
  } else if (used < 2) {
    audioBuffering = true;
  }

  if (!audioBuffering && isPlaying) {
    alignas(4) static int16_t out[AUDIO_PLAY_CHUNK / 2];
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
        Serial.printf("[RFID] Ausente %d/%d\n", missingChecks, MAX_DISC_MISSES);
        if (missingChecks >= MAX_DISC_MISSES) {
          Serial.println("[RFID] Disco retirado da vitrola!");
          handleDiscRemoved();
        }
      }
    } else {
      byte bufferATQA[2];
      byte bufferSize = sizeof(bufferATQA);
      bool tagDetected = false;

      // Tentativa 1: Ganho Maximo
      reinitRfidRegisters(MFRC522::RxGain_max);
      if (rfid.PICC_WakeupA(bufferATQA, &bufferSize) == MFRC522::STATUS_OK) {
        tagDetected = true;
      } else {
        bufferSize = sizeof(bufferATQA);
        if (rfid.PICC_RequestA(bufferATQA, &bufferSize) == MFRC522::STATUS_OK) {
          tagDetected = true;
        }
      }

      // Tentativa 2: Ganho Medio (33dB - evita saturacao a 0mm)
      if (!tagDetected) {
        reinitRfidRegisters(MFRC522::RxGain_33dB);
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
          char uid[32] = {0};
          for (byte i = 0; i < rfid.uid.size; i++) {
            snprintf(uid + strlen(uid), sizeof(uid) - strlen(uid), "%02X", rfid.uid.uidByte[i]);
          }
          rfid.PICC_HaltA();
          rfid.PCD_StopCrypto1();
          Serial.printf("[RFID] >>> DISCO DETECTADO! UID: %s (Tamanho: %d bytes) <<<\n", uid, rfid.uid.size);
          playTone(880.0f, 100, 0.15f); // Bip imediato de reconhecimento no alto-falante
          handleDiscPlaced(uid);
        } else {
          Serial.println("[RFID] Tag detectada no campo RF, mas falhou ao ler UID.");
        }
      }
      digitalWrite(PIN_RFID_SDA, HIGH);
    }
  }

  // 4. Sincronizacao de Status com Servidor STI (a cada 3s se tocando)
  if (now - lastStatusPoll >= 3000) {
    lastStatusPoll = now;
    if (isDiscPresent && isPlaying) {
      pollServerStatus();
    }
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
  // Motor N20
  pinMode(PIN_MOTOR_IN1, OUTPUT);
  pinMode(PIN_MOTOR_IN2, OUTPUT);
  stopMotor();

  // Teste rapido do Motor na inicializacao (1 segundo)
  Serial.println("[MOTOR TEST] Testando giro do motor por 1 segundo...");
  digitalWrite(PIN_MOTOR_IN2, LOW);
  analogWrite(PIN_MOTOR_IN1, 100);
  delay(1000);
  stopMotor();
  Serial.println("[MOTOR TEST] Motor parado.");

  // Barramento SPI do RFID no ESP32-C3
  pinMode(PIN_RFID_SDA, OUTPUT);
  digitalWrite(PIN_RFID_SDA, HIGH);
  SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SDA);
  rfid.PCD_Init();
  reinitRfidRegisters(MFRC522::RxGain_max);

  byte ver = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("[RFID] Registrador de Versao RC522: 0x%02X\n", ver);
  if (ver == 0x00 || ver == 0xFF) {
    Serial.println("[RFID ALERTA] Modulo RC522 nao respondeu (0x00/0xFF)! Verifique a ligacao SPI (GPIO 4 SCK, 5 MISO, 6 MOSI, 7 SDA, 3.3V, GND).");
  } else {
    Serial.printf("[RFID OK] Modulo RC522 pronto (Versao 0x%02X)!\n", ver);
  }

  // I2S MAX98357A
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("[ERRO] Falha ao inicializar I2S!");
  } else {
    Serial.println("[OK] I2S MAX98357A ativo na base.");
  }
}

void connectWifi() {
  Serial.printf("[WIFI] Conectando a %s...", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WIFI] Falha ao conectar no Wi-Fi.");
  }
}

void startMotor(int speed) {
  digitalWrite(PIN_MOTOR_IN2, LOW);
  // Kick inicial suave
  analogWrite(PIN_MOTOR_IN1, MOTOR_KICK_POWER);
  delay(MOTOR_KICK_TIME_MS);
  analogWrite(PIN_MOTOR_IN1, speed);
  Serial.printf("[MOTOR] Girando vinil a %d PWM (~33 RPM)\n", speed);
}

void stopMotor() {
  analogWrite(PIN_MOTOR_IN1, 0);
  digitalWrite(PIN_MOTOR_IN1, LOW);
  digitalWrite(PIN_MOTOR_IN2, LOW);
  Serial.println("[MOTOR] Prato parado.");
}

bool checkCardPresent(const char* targetUid) {
  byte bufferATQA[2];
  byte bufferSize = sizeof(bufferATQA);
  MFRC522::StatusCode status;

  // Tentativa 1: Ganho Maximo
  reinitRfidRegisters(MFRC522::RxGain_max);
  status = rfid.PICC_WakeupA(bufferATQA, &bufferSize);
  if (status != MFRC522::STATUS_OK) {
    bufferSize = sizeof(bufferATQA);
    status = rfid.PICC_RequestA(bufferATQA, &bufferSize);
  }

  // Tentativa 2: Ganho Medio (33dB)
  if (status != MFRC522::STATUS_OK) {
    reinitRfidRegisters(MFRC522::RxGain_33dB);
    bufferSize = sizeof(bufferATQA);
    status = rfid.PICC_WakeupA(bufferATQA, &bufferSize);
    if (status != MFRC522::STATUS_OK) {
      bufferSize = sizeof(bufferATQA);
      status = rfid.PICC_RequestA(bufferATQA, &bufferSize);
    }
  }

  // Tentativa 3: Se falhou, pausa PWM do motor brevemente e cicla antena RF
  if (status != MFRC522::STATUS_OK) {
    if (isPlaying) {
      analogWrite(PIN_MOTOR_IN1, 0);
      delay(15);
    }
    rfid.PCD_AntennaOff();
    delay(6);
    reinitRfidRegisters(MFRC522::RxGain_max);
    delay(6);

    bufferSize = sizeof(bufferATQA);
    status = rfid.PICC_RequestA(bufferATQA, &bufferSize);
    if (status != MFRC522::STATUS_OK) {
      bufferSize = sizeof(bufferATQA);
      status = rfid.PICC_WakeupA(bufferATQA, &bufferSize);
    }
    if (isPlaying) {
      analogWrite(PIN_MOTOR_IN1, MOTOR_DEFAULT_SPEED);
    }
  }

  if (status == MFRC522::STATUS_OK) {
    digitalWrite(PIN_RFID_SDA, HIGH);
    return true;
  }

  digitalWrite(PIN_RFID_SDA, HIGH);
  return false;
}

void handleDiscPlaced(const char* uidHex) {
  isDiscPresent = true;
  strncpy(activeUidHex, uidHex, sizeof(activeUidHex) - 1);
  missingChecks = 0;

  startMotor(MOTOR_DEFAULT_SPEED);

  // Avisa a tela CYD via Serial0 fisica e Serial USB
  Serial0.print("DISC_PLACED:");
  Serial0.println(uidHex);
  Serial.print("DISC_PLACED:");
  Serial.println(uidHex);

  // Faz requisicao ao Servidor STI
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClient client;
    String url = "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) + "/api/disc/place";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"uid\":\"" + String(uidHex) + "\"}";
    int code = http.POST(payload);
    if (code == 200) {
      String resp = http.getString();
      StaticJsonDocument<512> doc;
      deserializeJson(doc, resp);
      currentTrackTitle = doc["title"] | "Tocando";
      currentArtist     = doc["artist"] | "Spotify";
      currentDiscName   = doc["disc_name"] | "Vinil 3D";
      durationSeconds   = doc["duration_sec"] | 180;
      isPlaying         = true;
      Serial.printf("[STI] Play: %s - %s\n", currentTrackTitle.c_str(), currentArtist.c_str());
    }
    http.end();
  }

  sendStatusToCYD();
}

void handleDiscRemoved() {
  isDiscPresent = false;
  isPlaying     = false;
  strcpy(activeUidHex, "NENHUM");
  currentDiscName   = "Prato livre";
  currentTrackTitle = "Aproxime um vinil";
  currentArtist     = "Vitrola Jukebox";
  progressSeconds   = 0;

  stopMotor();

  // Limpa buffer de audio
  __atomic_store_n(&audioRingTail, __atomic_load_n(&audioRingHead, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
  audioBuffering = true;

  // Envia a CYD via Serial0 e USB
  Serial0.println("DISC_REMOVED");
  Serial.println("DISC_REMOVED");

  // Notifica Servidor STI
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClient client;
    String url = "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) + "/api/disc/remove";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.POST("{}");
    http.end();
  }

  sendStatusToCYD();
}

void sendStatusToCYD() {
  StaticJsonDocument<384> doc;
  doc["title"]    = currentTrackTitle;
  doc["artist"]   = currentArtist;
  doc["disc"]     = currentDiscName;
  doc["uid"]      = activeUidHex;
  doc["playing"]  = isPlaying;
  doc["present"]  = isDiscPresent;
  doc["progress"] = progressSeconds;
  doc["duration"] = durationSeconds;
  doc["vol"]      = currentVolume;

  // Envia via Serial0 fisica (GPIO 21 TX -> CYD RX)
  serializeJson(doc, Serial0);
  Serial0.println();

  // Envia via USB CDC (Monitor PC)
  serializeJson(doc, Serial);
  Serial.println();
}

void pollServerStatus() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  WiFiClient client;
  String url = "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) + "/api/status";
  http.begin(client, url);
  http.setTimeout(2500);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    deserializeJson(doc, payload);
    progressSeconds = doc["progress_sec"] | progressSeconds;
    durationSeconds = doc["duration_sec"] | durationSeconds;
    isPlaying       = doc["playing"] | isPlaying;
    sendStatusToCYD();
  }
  http.end();
}

void pumpNetworkAudio() {
  if (!audioClient.connected()) return;
  static uint8_t tmp[1024];
  for (int i = 0; i < 8; i++) {
    int avail = audioClient.available();
    uint32_t space = audioRingFree();
    if (avail <= 0 || space == 0) break;
    int toRead = min((uint32_t)avail, min(space, (uint32_t)sizeof(tmp)));
    int r = audioClient.read(tmp, toRead);
    if (r <= 0) break;
    audioRingWrite(tmp, r);
  }
}

void playTone(float freqHz, int durationMs, float amp) {
  const int chunkSamples = 128;
  alignas(4) int16_t buf[chunkSamples];
  int totalSamples = (SAMPLE_RATE * durationMs) / 1000;
  float gain = (currentVolume / 100.0f) * amp * 32000.0f;
  double phase = 0.0, inc = (2.0 * M_PI * freqHz) / (double)SAMPLE_RATE;
  int idx = 0;
  for (int i = 0; i < totalSamples; i++) {
    buf[idx++] = (int16_t)(sinf((float)phase) * gain);
    if (idx >= chunkSamples) { i2s.write((const uint8_t*)buf, idx * 2); idx = 0; }
    phase += inc;
    if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
  }
  if (idx > 0) i2s.write((const uint8_t*)buf, idx * 2);
}
