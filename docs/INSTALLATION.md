# Installation & Build

## Deutsch
Voraussetzungen: Arduino IDE 2.x, ESP8266 Arduino Core 3.1.2, ESP8266 D1 mini mit 4 MB Flash.

Libraries: Adafruit GFX, Adafruit PCD8544, Adafruit VL53L0X, Adafruit VL53L1X, PubSubClient.

1. Board **LOLIN(WEMOS) D1 R2 & mini** wählen.
2. Sprache in `Language.h` wählen.
3. `firmware/Fuellstandsmesser_classic/Fuellstandsmesser_classic.ino` öffnen.
4. Alle .cpp/.h-Dateien und `languages/` im Sketchordner belassen.
5. Kompilieren und flashen.
6. Serial Monitor: **115200 Baud**.

Fallback-AP: `Fuellstandsmesser_classic-<chipid>`. Web OTA: `http://<geraete-ip>/update`. ArduinoOTA ist aus RAM-Gründen entfernt.

## English
Requirements: Arduino IDE 2.x, ESP8266 Arduino Core 3.1.2, ESP8266 D1 mini with 4 MB flash.

Libraries: Adafruit GFX, Adafruit PCD8544, Adafruit VL53L0X, Adafruit VL53L1X, PubSubClient.

1. Select **LOLIN(WEMOS) D1 R2 & mini**.
2. Select the language in `Language.h`.
3. Open `firmware/Fuellstandsmesser_classic/Fuellstandsmesser_classic.ino`.
4. Keep all .cpp/.h files and `languages/` in the sketch folder.
5. Compile and flash.
6. Serial Monitor: **115200 baud**.

Fallback AP: `Fuellstandsmesser_classic-<chipid>`. Web OTA: `http://<device-ip>/update`. ArduinoOTA is removed to save RAM.
