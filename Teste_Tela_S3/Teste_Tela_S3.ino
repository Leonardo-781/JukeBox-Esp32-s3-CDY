#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// Pinos ESP32-S3 definidos para a tela:
#define PIN_TFT_CS    10
#define PIN_TFT_DC    9
#define PIN_TFT_RST   14
#define PIN_SPI_SCK   12
#define PIN_SPI_MOSI  11
#define PIN_SPI_MISO  13

Adafruit_ILI9341 tft = Adafruit_ILI9341(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- TESTE DEDICADO DE TELA TFT NO ESP32-S3 ---");
  Serial.printf("Pinos: CS=%d, DC=%d, RST=%d, SCK=%d, MOSI=%d\n",
                PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, PIN_SPI_SCK, PIN_SPI_MOSI);

  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(20);
  digitalWrite(PIN_TFT_RST, LOW);
  delay(50);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(100);

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  Serial.println("[TFT] Inicializando ILI9341...");
  tft.begin(20000000);
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
  tft.println("ESP32-S3 OK!");
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);
  tft.setCursor(30, 130);
  tft.println("TELA FUNCIONANDO!");
  Serial.println("[TFT] Teste concluido!");
}

void loop() {
  delay(500);
}
