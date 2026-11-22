#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP_I2S.h>   // Requer core ESP32 Arduino 3.x
#include <math.h> 

// ==============================================================================
// ESP32-S3 + MAX98357A | Dispositivo "Jukebox" do Spotify
// ------------------------------------------------------------------------------
// No PC rode:  python servidor_jukebox/ponte_audio.py
// No celular: Spotify -> escolha a musica -> Dispositivos -> "Jukebox"
// O audio chega aqui por TCP (porta 3004) em PCM 16-bit MONO 44.1 kHz.
// ==============================================================================
#define WIFI_SSID        "SUA_REDE_WIFI"
#define WIFI_PASS        "SUA_SENHA_WIFI"
#define SERVER_HOST      "192.168.0.105"   // IP do PC que roda a ponte
#define API_PORT         3003              // REST do app.py (opcional p/ botao e disco)
#define AUDIO_PORT       3004              // Ponte de audio (ponte_audio.py)
#define TEST_DISC_UID    "043963AF"

// ==============================================================================
// PINAGEM ESP32-S3 -> MAX98357A
// ==============================================================================
#define PIN_I2S_BCLK     15    // BCLK
#define PIN_I2S_LRC      16    // LRC
#define PIN_I2S_DOUT     17    // DIN
#define PIN_BTN_BOOT     0     // Botao BOOT (Play/Pause via REST)

// ==============================================================================
// AUDIO
// ==============================================================================
#define SAMPLE_RATE      44100
#define RING_SIZE        65536   // ~743 ms de folga (potencia de 2!)
#define RING_MASK        (RING_SIZE - 1)
#define PREFILL_BYTES    16384   // ~186 ms acumulados antes de comecar a tocar
#define PLAY_CHUNK       1024    // bytes por escrita no I2S (512 amostras)

I2SClass i2s;
WiFiClient audioClient;

// Buffer circular produtor (loop/TCP) -> consumidor (task de audio)
alignas(4) static uint8_t ring[RING_SIZE];
static volatile uint32_t ringHead = 0;   // total de bytes escritos
static volatile uint32_t ringTail = 0;   // total de bytes lidos

volatile int  volumePercent  = 80;       // Volume local extra (o do app Spotify tambem funciona)
volatile bool toneRequest    = false;
volatile uint32_t underruns  = 0;
volatile bool isPlaying      = false;

unsigned long lastReconnect  = 0;
unsigned long lastStatsMs    = 0;
uint32_t bytesRecebidos      = 0;

int lastBtnState             = HIGH;
unsigned long lastBtnMs      = 0;

// ------------------------------------------------------------------------------
static inline uint32_t ringUsed() {
  uint32_t h = __atomic_load_n(&ringHead, __ATOMIC_ACQUIRE);
  uint32_t t = __atomic_load_n(&ringTail, __ATOMIC_ACQUIRE);
  return h - t;
}
static inline uint32_t ringFree() { return RING_SIZE - ringUsed(); }

// Produtor: copia n bytes para o ring (chamar so com n <= ringFree())
static void ringWrite(const uint8_t* src, uint32_t n) {
  uint32_t h = ringHead;
  uint32_t pos = h & RING_MASK;
  uint32_t first = min(n, (uint32_t)(RING_SIZE - pos));
  memcpy(ring + pos, src, first);
  if (n > first) memcpy(ring, src + first, n - first);
  __atomic_store_n(&ringHead, h + n, __ATOMIC_RELEASE);
}

// Consumidor: retira n bytes do ring
static void ringRead(uint8_t* dst, uint32_t n) {
  uint32_t t = ringTail;
  uint32_t pos = t & RING_MASK;
  uint32_t first = min(n, (uint32_t)(RING_SIZE - pos));
  memcpy(dst, ring + pos, first);
  if (n > first) memcpy(dst + first, ring, n - first);
  __atomic_store_n(&ringTail, t + n, __ATOMIC_RELEASE);
}

// ==============================================================================
// SONS LOCAIS
// ==============================================================================
void playToneMono(float freqHz, int durationMs, float amp = 0.25f) {
  const int chunkSamples = 256;
  alignas(4) int16_t buf[chunkSamples];
  int totalSamples = (SAMPLE_RATE * durationMs) / 1000;
  int fadeSamples  = min(totalSamples / 4, (SAMPLE_RATE * 15) / 1000);
  float gain = (volumePercent / 100.0f) * amp * 32000.0f;
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
  playToneMono(523.25f, 110, 0.22f);
  playToneMono(659.25f, 110, 0.22f);
  playToneMono(783.99f, 180, 0.25f);
}

// ==============================================================================
// TASK DE AUDIO: unica dona do I2S. Toca o ring ou silencio (nunca deixa o DMA
// repetir o ultimo bloco, que era o "zumbido" quando faltava dado).
// ==============================================================================
void audioTask(void*) {
  alignas(4) static int16_t out[PLAY_CHUNK / 2];
  bool buffering = true;

  for (;;) {
    if (toneRequest) {
      playStartupChime();
      toneRequest = false;
    }

    uint32_t used = ringUsed();

    if (buffering) {
      if (used >= PREFILL_BYTES) {
        buffering = false;
        isPlaying = true;
      }
    } else if (used < 2) {
      buffering = true;           // acabou o audio (pausa ou Wi-Fi lento)
      isPlaying = false;
      underruns++;
    }

    if (!buffering) {
      uint32_t n = min(used, (uint32_t)PLAY_CHUNK) & ~1u;   // sempre amostras inteiras
      ringRead((uint8_t*)out, n);
      int count = n / 2;
      int vol = volumePercent;
      if (vol < 100) {
        for (int i = 0; i < count; i++) out[i] = (int16_t)(((int32_t)out[i] * vol) / 100);
      }
      i2s.write((const uint8_t*)out, n);     // bloqueia -> ritmo exato de 44.1 kHz
    } else {
      memset(out, 0, sizeof(out));
      i2s.write((const uint8_t*)out, 256 * 2);   // ~5.8 ms de silencio
    }
  }
}

// ==============================================================================
// REDE
// ==============================================================================
bool sendJukeboxPost(const String& path, const String& jsonBody) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.begin("http://" + String(SERVER_HOST) + ":" + String(API_PORT) + path);
  http.setConnectTimeout(800);
  http.setTimeout(1200);   // curto p/ nao esvaziar o buffer de audio
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(jsonBody);
  String resp = (code > 0) ? http.getString() : "";
  http.end();
  Serial.printf("[REST] POST %s -> HTTP %d %s\n", path.c_str(), code, resp.c_str());
  return (code == 200 || code == 204);
}

void connectAudioStream() {
  Serial.printf("[AUDIO] Conectando na ponte %s:%d...\n", SERVER_HOST, AUDIO_PORT);
  if (audioClient.connect(SERVER_HOST, AUDIO_PORT, 3000)) {
    audioClient.setNoDelay(true);
    // Se a conexao anterior caiu no meio de uma amostra, completa com 1 byte
    // para manter o alinhamento de 16 bits.
    if (ringHead & 1) { uint8_t z = 0; ringWrite(&z, 1); }
    Serial.println("[AUDIO] Conectado! Escolha a musica no app do Spotify -> Dispositivos -> Jukebox");
  } else {
    Serial.println("[AUDIO] Ponte nao respondeu. O ponte_audio.py esta rodando no PC?");
  }
}

void pumpNetworkAudio() {
  // Le do TCP somente o que cabe no ring: se o ring encher, o TCP segura o PC
  // (contrapressao) e a musica anda no ritmo certo.
  static uint8_t tmp[4096];
  for (int i = 0; i < 8; i++) {
    int avail = audioClient.available();
    uint32_t space = ringFree();
    if (avail <= 0 || space == 0) break;
    int toRead = min((uint32_t)avail, min(space, (uint32_t)sizeof(tmp)));
    int r = audioClient.read(tmp, toRead);
    if (r <= 0) break;
    ringWrite(tmp, r);
    bytesRecebidos += r;
  }
}

// ==============================================================================
// MENU E ENTRADAS
// ==============================================================================
void printMenu() {
  Serial.println("\n========================================================");
  Serial.println("  ESP32-S3 + MAX98357A | SPOTIFY CONNECT \"Jukebox\"");
  Serial.println("========================================================");
  Serial.printf("  I2S -> BCLK %d | LRC %d | DIN %d\n", PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
  Serial.printf("  Ponte audio %s:%d | REST %s:%d\n", SERVER_HOST, AUDIO_PORT, SERVER_HOST, API_PORT);
  Serial.println("  App Spotify: escolha a musica -> Dispositivos -> Jukebox");
  Serial.println("  Serial: [+]/[-] volume | [p] play/pause | [n] proxima");
  Serial.println("          [d] disco " TEST_DISC_UID " | [t] som de teste | [h] menu");
  Serial.println("========================================================\n");
}

void handleInputs() {
  int btn = digitalRead(PIN_BTN_BOOT);
  if (btn == LOW && lastBtnState == HIGH && (millis() - lastBtnMs > 300)) {
    lastBtnMs = millis();
    Serial.println("[BOOT] Play / Pause");
    sendJukeboxPost("/api/player/toggle", "{}");
  }
  lastBtnState = btn;

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '+' || c == '=')      { volumePercent = min(100, volumePercent + 5); Serial.printf("[VOLUME] %d%%\n", volumePercent); }
    else if (c == '-' || c == '_') { volumePercent = max(5, volumePercent - 5);   Serial.printf("[VOLUME] %d%%\n", volumePercent); }
    else if (c == 'p' || c == 'P') sendJukeboxPost("/api/player/toggle", "{}");
    else if (c == 'n' || c == 'N') sendJukeboxPost("/api/player/next", "{}");
    else if (c == 'd' || c == 'D') sendJukeboxPost("/api/disc/place", String("{\"uid\":\"") + TEST_DISC_UID + "\"}");
    else if (c == 't' || c == 'T') { Serial.println("[TESTE] Som local"); toneRequest = true; }
    else if (c == 'h' || c == 'H' || c == '?') printMenu();
  }
}

// ==============================================================================
// SETUP / LOOP
// ==============================================================================
void setup() {
  Serial.begin(115200);
  delay(600);
  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
  printMenu();

  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("[ERRO] Falha ao inicializar I2S!");
    while (true) delay(1000);
  }
  Serial.println("[OK] I2S 44100 Hz, 16-bit MONO");
  playStartupChime();

  // Task de audio com prioridade alta; passa a maior parte do tempo bloqueada no i2s.write
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 5, nullptr, 1);

  Serial.printf("[WIFI] Conectando em %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(300); Serial.print("."); }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
    connectAudioStream();
  } else {
    Serial.println("\n[ERRO WIFI] Verifique SSID/Senha (rede 2.4 GHz).");
  }
}

void loop() {
  handleInputs();

  if (WiFi.status() == WL_CONNECTED) {
    if (!audioClient.connected()) {
      if (millis() - lastReconnect >= 2000) {
        lastReconnect = millis();
        connectAudioStream();
      }
    } else {
      pumpNetworkAudio();
    }
  }

  if (millis() - lastStatsMs >= 5000) {
    lastStatsMs = millis();
    if (bytesRecebidos > 0 || isPlaying) {
      Serial.printf("[STREAM] %lu KB/s (ideal ~86) | buffer %lu ms | %s | engasgos %lu | vol %d%%\n",
                    (unsigned long)(bytesRecebidos / 5120),
                    (unsigned long)(ringUsed() * 1000UL / (SAMPLE_RATE * 2)),
                    isPlaying ? "tocando" : "aguardando",
                    (unsigned long)underruns, volumePercent);
    }
    bytesRecebidos = 0;
  }

  delay(1);
}
