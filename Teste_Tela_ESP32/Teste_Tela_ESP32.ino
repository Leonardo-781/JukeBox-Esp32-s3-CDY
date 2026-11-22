#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// Definicao de Pinos para o ESP32 Classico (38 pinos)
#define PIN_TFT_CS    5
#define PIN_TFT_DC    4
#define PIN_TFT_RST   2
#define PIN_SPI_SCK   18
#define PIN_SPI_MOSI  23
#define PIN_SPI_MISO  19

Adafruit_ILI9341 tft = Adafruit_ILI9341(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=================================================");
  Serial.println("  TESTE DE TELA TFT ILI9341 - ESP32 CLASSICO");
  Serial.println("=================================================");
  Serial.printf("Pinos: CS=%d, DC=%d, RST=%d, SCK=%d, MOSI=%d\n",
                PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, PIN_SPI_SCK, PIN_SPI_MOSI);

  // Pulso de Reset manual garantido
  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(50);
  digitalWrite(PIN_TFT_RST, LOW);
  delay(100);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(150);

  // Inicializa SPI nativo
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  // Inicializa ILI9341 com clock mais tolerante para jumpers (10 MHz)
  Serial.println("[TFT] Inicializando ILI9341 a 10MHz...");
  tft.begin(10000000);
  tft.setRotation(3);

  Serial.println("[TFT] Pintando tela de VERMELHO...");
  tft.fillScreen(ILI9341_RED);
  delay(1000);

  Serial.println("[TFT] Pintando tela de VERDE...");
  tft.fillScreen(ILI9341_GREEN);
  delay(1000);

  Serial.println("[TFT] Pintando tela de AZUL...");
  tft.fillScreen(ILI9341_BLUE);
  delay(1000);

  Serial.println("[TFT] Pintando tela de PRETO com texto...");
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(3);
  tft.setCursor(30, 80);
  tft.println("ESP32 OK!");
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);
  tft.setCursor(30, 130);
  tft.println("TELA FUNCIONANDO!");
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(30, 170);
  tft.println("Volume: 50%");
  Serial.println("[TFT] Teste inicial concluido com sucesso!");
}

void loop() {
  delay(1000);
}
