# 📻 Vitrola Jukebox Retrô Inteligente — ESP32-S3 & CYD 2-USB

Projeto completo de engenharia eletrônica, marcenaria e impressão 3D de uma **Vitrola Jukebox Inteligente** com prato giratório motorizado, leitura de mini discos de vinil via RFID (13.56 MHz), reprodução de áudio Hi-Fi (I2S MAX98357A), painel touch screen colorido com capa de vinil animada e arquitetura híbrida com **Modo 100% Offline (MicroSD)** e **Modo Online (Spotify / Nuvem Global)**.

![Desenho Técnico Vitrola](3D/Desenho_Tecnico_Vitrola_Console.png)

---

## 🌟 Versões do Projeto no Repositório

O repositório está organizado em duas versões dedicadas para atender a qualquer cenário de uso:

| Versão | Branch | Tag | Descrição Principal |
| :--- | :---: | :---: | :--- |
| **⚡ Versão Offline (Recomendada)** | `main` / `versao-offline` | `v2.0-offline` | **100% Autônoma:** Músicas e catálogo de tags gravados direto no Cartão MicroSD. Áudio transmitido a 1.5 Mbps UART. Portal AP (`192.168.4.1`) para cadastro de tags no prato sem precisar de internet! |
| **🌐 Versão Online** | `versao-online` | `v1.0-online` | **Conectada à Nuvem:** Streaming via Spotify API, servidor backend local (Flask/Node) ou nuvem mundial (`neoface.dev.br`), busca dinâmica e sincronização remota. |

---

## ⚡ Destaques da Arquitetura Offline (`v2.0-offline`)

* **Zero Dependência de Internet:**
  * Não depende de conexão Wi-Fi ou servidores externos para iniciar o som.
  * O catálogo de discos e vínculos de tags RFID ficam salvos no arquivo `/discs.json` diretamente no cartão MicroSD da tela CYD.
* **Reprodução Ultra Rápida (< 30 ms):**
  * Ao colocar a tag no prato, o ESP32-S3 notifica a tela CYD via UART em tempo real.
  * O CYD lê o áudio PCM 16-bit 44.1 kHz direto do MicroSD e transmite em blocos de alta velocidade (**1.500.000 baud**) diretamente para o **Buffer de 1 Megabyte em PSRAM Octal** do ESP32-S3.
  * A música começa instantaneamente, sem travamentos e com áudio contínuo.
* **Portal de Cadastro AP no Prato (`http://192.168.4.1`):**
  * Para cadastrar novos discos, basta segurar o botão por **3 segundos** ou tocar na tela.
  * O ESP32-S3 cria o Wi-Fi **`Jukebox-Setup`**. Ao conectar seu celular ou notebook, abre-se um painel responsivo.
  * **Detecção no Prato:** Basta aproximar o disco da vitrola; o portal detecta o UID da tag em tempo real (`🟢 Tag Detectada`) e preenche o formulário automaticamente.
  * **Catálogo de 150 Músicas Embutido:** Selecione a música desejada e clique em `Salvar no MicroSD`. A vitrola emite um sinal sonoro e grava permanentemente no cartão!

---

## 🗂️ Estrutura do Repositório

```text
├── 3D/                          # Modelos CAD em formato STL prontos para impressão 3D
│   ├── Vitrola_Console_Gabinete.stl
│   ├── Vitrola_Gabinete_Base.stl
│   ├── Vitrola_Prato_Giratorio.stl
│   ├── Vitrola_Knob_Volume.stl
│   ├── Vitrola_Tampa_Superior.stl
│   ├── Vitrola_Tampa_Fundo.stl
│   └── Desenho_Tecnico_Vitrola_Console.png
├── S3_Base/                     # Firmware do ESP32-S3 (Base, RFID, I2S, Motor, Encoder, Portal AP)
│   ├── S3_Base.ino
│   ├── ap_portal.h              # Interface Web moderna embutida na memória Flash (PROGMEM)
│   └── track_catalog.h          # Catálogo PROGMEM de faixas offline para busca rápida
├── CYD_Display/                 # Firmware da Tela CYD 2-USB (ST7789, Touch, MicroSD Storage & Audio Stream)
│   ├── CYD_Display.ino
│   └── track_catalog.h          # Resolução local de títulos e metadados
├── C3_Base/                     # Firmware alternativo para ESP32-C3
│   └── C3_Base.ino
├── servidor_jukebox/            # Backend (usado na Versão Online - Flask + Spotify Connect)
│   ├── app.py
│   ├── ponte_audio.py
│   ├── discos.json
│   ├── requirements.txt
│   └── spotify_config.example.json
├── cyd_std_user_setup.h         # Configuração de pinagem do TFT_eSPI para a placa CYD
├── gerar_modelo_3d.py           # Gerador paramétrico dos modelos STL via Python
├── gerar_desenho_tecnico.py     # Gerador de plantas e desenho técnico 2D
└── README.md
```

---

## 💾 Estrutura do Cartão MicroSD (CYD)

O cartão MicroSD (formatado em FAT32) deve ser inserido no slot lateral da tela CYD:

```text
CARTAO_SD/
├── discs.json                   # Banco de dados associando UIDs de RFID às faixas
├── audio/                       # Músicas em formato PCM bruto (16-bit, 44.1 kHz ou 22.05 kHz Mono)
│   ├── 0FZd4laxB12sUTKSUUGwWq.s16
│   ├── 3Me7nZwb283YVVxTT4eJLe.s16
│   ├── 1uDn0nbzeGPVw97jp7SvzR.s16
│   └── ...
└── covers/                      # Capas dos álbuns pré-renderizadas em RGB565 binário (opcional)
    ├── 0FZd4laxB12sUTKSUUGwWq.bin
    └── ...
```

### Formato do arquivo `discs.json`:
```json
{
  "043963AF": {
    "name": "STORM II",
    "artist": "Vários",
    "track": "0FZd4laxB12sUTKSUUGwWq",
    "uri": "spotify:track:0FZd4laxB12sUTKSUUGwWq"
  },
  "041D43AF8B2681": {
    "name": "A Sua Maneira",
    "artist": "Capital Inicial",
    "track": "21S2akASCzAq2ZJCMK0l2A",
    "uri": "spotify:track:21S2akASCzAq2ZJCMK0l2A"
  }
}
```

---

## ⚡ Esquema de Ligação & Pinagem Completa

### 1. Comunicação Serial Inter-ESP (1.500.000 Baud)
A conexão serial de alta velocidade entre os dois ESPs deve ter ligação cruzada e terra compartilhado:

| ESP32-S3 (Base) | CYD 2-USB (Display) | Função |
| :--- | :--- | :--- |
| **GPIO 8 (RX)** | **TX (GPIO 1 / Conector CN1)** | Recebe telemetria e blocos de áudio PCM do MicroSD |
| **GPIO 18 (TX)** | **RX (GPIO 3 / Conector CN1)** | Envia telemetria, comandos de controle e UIDs RFID |
| **GND** | **GND** | **Referência de terra comum (obrigatório)** |

---

### 2. Pinos do ESP32-S3 N16R8 (Base da Vitrola)

| Módulo / Periférico | Pino no Módulo | Pino no ESP32-S3 | Observações |
| :--- | :--- | :--- | :--- |
| **Leitor RFID RC522** | SDA / CS | **GPIO 10** | Alimentação estrita de **3.3V** |
| | SCK | **GPIO 12** | Barramento SPI de Hardware |
| | MOSI | **GPIO 11** | Ganho de antena calibrado no firmware (+48dB) |
| | MISO | **GPIO 13** | |
| | RST | **GPIO 14** (ou 3.3V) | Reset de Hardware |
| **Áudio I2S (MAX98357A)** | BCLK | **GPIO 15** | Bit Clock |
| | LRC / WS | **GPIO 16** | Left/Right Clock (Word Select) |
| | DIN | **GPIO 17** | Digital Audio Input |
| | GAIN | **GND** / Resistor | Ajuste de ganho (+12dB) |
| **Motor DC N20 (Ponte H)** | IN1 (PWM) | **GPIO 4** | Controle de velocidade do prato com Kickstart |
| | IN2 (Sentido) | **GPIO 5** | Sentido de rotação / Freio |
| **Encoder Rotativo** | CLK | **GPIO 6** | Canal A |
| | DT | **GPIO 7** | Canal B |
| | SW (Botão) | **GPIO 9** | Curto: Play/Pause \| Longo (3s): Ativa Portal AP |

---

### 3. Pinos do CYD (Cheap Yellow Display ESP32-2432S028R)

| Periférico Interno CYD | Pinos Mapeados | Observações |
| :--- | :--- | :--- |
| **Display ST7789 2.8"** | MOSI: 13, SCLK: 14, CS: 15, DC: 2 | Resolução 320x240 RGB565 |
| **Backlight Display** | **GPIO 21** | Controle de brilho PWM |
| **Touch XPT2046** | MOSI: 32, MISO: 39, CLK: 25, CS: 33, IRQ: 36 | Touchscreen resistivo calibrado |
| **Slot MicroSD Onboard**| SCK: 18, MISO: 19, MOSI: 23, CS: 5 | Barramento VSPI dedicado de alta velocidade |
| **Porta Serial UART0** | TX: **GPIO 1**, RX: **GPIO 3** | Conectores CN1 ou P1 para ligação com o S3 |

---

## 🛠️ Como Compilar e Gravar os Firmwares

### Pré-requisitos
* **Arduino IDE 2.x** ou **arduino-cli**
* Pacote de placas **esp32 by Espressif Systems** (versão 3.0.x ou superior)
* Bibliotecas necessárias no Arduino:
  * `TFT_eSPI` (copie `cyd_std_user_setup.h` para `User_Setup.h`)
  * `ArduinoJson` (v6 ou v7)
  * `MFRC522` (v1.4.11+)
  * `ESP_I2S` (nativa do core ESP32 v3.x)

### 1. Gravando a Tela CYD (ESP32-2432S028R)
```bash
# Compilar
arduino-cli compile --fqbn esp32:esp32:esp32 --build-path ./CYD_Display/build ./CYD_Display/CYD_Display.ino

# Gravar (exemplo na porta COM11 a 921600 baud)
python -m esptool --chip esp32 --port COM11 --baud 921600 write-flash 0x0 ./CYD_Display/build/CYD_Display.ino.merged.bin
```

### 2. Gravando o ESP32-S3 N16R8 (Base)
> **Importante:** Configure a placa com **PSRAM = OPI PSRAM** (Octal) e **Flash Size = 16MB**.
```bash
# Compilar
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=default --build-path ./S3_Base/build ./S3_Base/S3_Base.ino

# Gravar (exemplo na porta COM14)
python -m esptool --chip esp32s3 --port COM14 --baud 921600 write-flash 0x0 ./S3_Base/build/S3_Base.ino.bootloader.bin 0x8000 ./S3_Base/build/S3_Base.ino.partitions.bin 0xe000 ./S3_Base/build/boot_app0.bin 0x10000 ./S3_Base/build/S3_Base.ino.bin
```

---

## 📖 Guia de Operação da Vitrola

### Como Ouvir Músicas (Modo Normal)
1. Ligue a vitrola conectando a fonte de alimentação 5V (2A+ recomendado).
2. Coloque um dos mini discos com tag RFID sobre o prato giratório.
3. O prato começará a girar automaticamente com o som vintage da agulha descendo e a música começará a tocar instantaneamente pelo alto-falante.
4. Gire o encoder rotativo para regular o volume de 0% a 100%. Dê um clique rápido para pausar ou continuar.

### Como Cadastrar uma Nova Tag (Modo AP)
1. Segure o botão giratório (encoder) por **3 segundos**, ou dê um toque na barra inferior da tela do CYD.
2. A tela exibirá o aviso de **Modo Setup**.
3. No seu smartphone ou computador, conecte à rede Wi-Fi **`Jukebox-Setup`**.
4. Acesse o endereço `http://192.168.4.1` no seu navegador.
5. Coloque a nova tag no prato da vitrola: o portal mostrará o UID em verde automaticamente.
6. Escolha a música no catálogo de faixas, preencha o nome do disco e clique em **`💾 Gravar no Cartão SD`**.
7. Clique em **`✓ Concluir & Voltar ao Modo Vitrola`** ou dê um toque na tela do CYD para voltar a ouvir seus discos!

---

## 🖨️ Modelos para Impressão 3D

Na pasta [`3D/`](3D/) estão os arquivos STL prontos para fatiamento:
* **`Vitrola_Console_Gabinete.stl`**: Gabinete principal com estilo retrô e alojamento para alto-falante.
* **`Vitrola_Prato_Giratorio.stl`**: Prato motorizado para acomodar os discos com tag RFID embutida.
* **`Vitrola_Knob_Volume.stl`**: Manípulo estilizado para o encoder rotativo.
* **`Vitrola_Tampa_Superior.stl` / `Vitrola_Tampa_Fundo.stl`**: Fechamento do console e encaixes internos.

---

## 👤 Autor

* **Leonardo Cardoso** — [@Leonardo-781](https://github.com/Leonardo-781)
* Projeto desenvolvido com foco em computação física, áudio digital embarcado e preservação da experiência nostálgica do vinil.
