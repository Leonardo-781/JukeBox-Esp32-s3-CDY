#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==============================================================================
// 1. PINAGEM OFICIAL - ESP32 CLÁSSICO / DEV MODULE (38 PINOS)
// ==============================================================================

// --- Barramento SPI Compartilhado (VSPI Hardware Padrão) ---
#define PIN_SPI_SCK       18   // Clock SPI (Display SCK + RFID SCK)
#define PIN_SPI_MOSI      23   // Dados de Saída SPI (Display MOSI/SDI + RFID MOSI)
#define PIN_SPI_MISO      19   // Dados de Entrada SPI (RFID MISO + Display SDO se houver)

// --- Leitor RFID RC522 (13.56 MHz - OBRIGATÓRIO 3.3V) ---
#define PIN_RFID_SDA      21   // Chip Select do RFID (SDA / SS)
#define PIN_RFID_RST      22   // Reset do RFID (ou no 3.3V)

// --- Display 2.8 TFT SPI (ILI9341 240x320) ---
#define PIN_TFT_CS        5    // Chip Select da Tela (CS)
#define PIN_TFT_DC        4    // Data / Command (DC / RS)
#define PIN_TFT_RST       2    // Reset da Tela (RST)
#define PIN_TFT_BL        -1   // Backlight ligado no 3.3V (ou 5V com resistor)
#define TFT_ROTATION      3    // Orientação Paisagem (Conector na lateral)

// --- Mini Ponte H (Motor DC do Prato de Vinil N20) ---
#define PIN_MOTOR_IN1     13   // Sinal PWM de velocidade
#define PIN_MOTOR_IN2     14   // Sentido / Freio (GND / LOW)

// --- Amplificador de Áudio I2S (MAX98357A / NS4168) ---
#define PIN_I2S_BCLK      26   // Bit Clock (BCLK)
#define PIN_I2S_LRC       25   // Left/Right Clock (LRC / WS)
#define PIN_I2S_DIN       27   // Serial Data In (DIN)

// --- Encoder Rotativo (Volume e Play/Pause) ---
#define PIN_ENC_CLK       32   // Canal A do Encoder (CLK)
#define PIN_ENC_DT        33   // Canal B do Encoder (DT)
#define PIN_ENC_SW        16   // Botão integrado (Push Button / SW)

// ==============================================================================
// 2. PARÂMETROS DO MOTOR E LEITURA RFID COM ALTA TOLERÂNCIA
// ==============================================================================
#define MOTOR_DEFAULT_SPEED   65    // Rotação suave de vitrola (~33 RPM)
#define MOTOR_KICK_POWER      115   // Pulso inicial para destravar o atrito estático
#define MOTOR_KICK_TIME_MS    80    // Duração do pulso de kick em milissegundos
#define RFID_CHECK_INTERVAL   400   // Intervalo de verificação do leitor RFID (ms)
#define MAX_DISC_MISSES       10    // 10 falhas consecutivas (~4.0 seg) para tolerar rotação
#define VINYL_ANIM_INTERVAL_MS 450  // Intervalo para rotação suave da capa no vinil

// ==============================================================================
// 3. CONEXÃO DE REDE E SERVIDOR STI (BACKEND SPOTIFY)
// ==============================================================================
#define DEFAULT_WIFI_SSID     "SUA_REDE_WIFI"
#define DEFAULT_WIFI_PASS     "SUA_SENHA_WIFI"
#define SERVER_HOST           "192.168.0.105"
#define SERVER_PORT           3003
#define AUDIO_PORT            3004
#define DEFAULT_CLOUD_URL     "https://neoface.dev.br/jukebox"

// --- Parâmetros de Áudio I2S e Streaming ---
#define SAMPLE_RATE           44100
#define AUDIO_RING_SIZE       65536   // 64 KB (~743 ms de folga)
#define AUDIO_PREFILL_BYTES   16384   // ~186 ms pré-buffer antes de iniciar
#define AUDIO_PLAY_CHUNK      1024    // bytes por bloco I2S

// ==============================================================================
// 4. PALETA DE CORES (TEMA RETRO DARK / SPOTIFY GREEN)
// ==============================================================================
#define COL_BG                0x0000  // #000000 Preto puro profundo
#define COL_CARD              0x10A2  // #10141a Fundo de cards
#define COL_EDGE              0x2965  // #282e38 Bordas e linhas
#define COL_WHITE             0xFFFF  // Branco puro
#define COL_MUTED             0x8410  // Cinza intermediário
#define COL_SPOTIFY           0x1E46  // #1db954 Verde Spotify oficial
#define COL_AMBER             0xFDE0  // Amarelo / Dourado vintage
#define COL_RED               0xF800  // Vermelho de alerta
#define COL_VINYL             0x1082  // Preto vinil reflexivo

#endif // CONFIG_H
