# Installation & Build

## Voraussetzungen

- Arduino IDE 2.x
- ESP8266 Arduino Core **3.1.2**
- USB-Verbindung zum ESP8266 D1 mini

## Board-Paket

ESP8266 Board Manager installieren und ein passendes D1-mini-Profil auswählen, z. B.:

- **LOLIN(WEMOS) D1 R2 & mini**

Die Firmware wurde mit 4 MB Flash betrieben.

## Libraries

Über den Arduino Library Manager installieren:

- Adafruit GFX Library
- Adafruit PCD8544 Nokia 5110 LCD library
- Adafruit VL53L0X
- Adafruit VL53L1X
- PubSubClient

## Sketch öffnen

Die Arduino-kompatible Sketchmappe liegt hier:

```text
firmware/Fuellstandsmesser_classic/
```

Öffne:

```text
firmware/Fuellstandsmesser_classic/Fuellstandsmesser_classic.ino
```

Alle `.cpp`- und `.h`-Dateien müssen im gleichen Sketchordner bleiben.

## Flashen

1. Board und COM-Port wählen.
2. Sketch kompilieren.
3. Auf den D1 mini hochladen.
4. Serial Monitor auf **115200 Baud** öffnen.

## Erster Start

Wenn keine gültigen WLAN-Zugangsdaten gespeichert sind oder die Verbindung länger ausfällt, startet der Fallback-AP:

```text
Fuellstandsmesser_classic-<chipid>
```

Danach kann die Weboberfläche zur Konfiguration verwendet werden.

## OTA

ArduinoOTA wurde zugunsten des RAM-Budgets entfernt.

Firmwareupdates erfolgen über **Web OTA**:

```text
http://<geraete-ip>/update
```

## Release-Build prüfen

Nach dem Flash sollten im seriellen Log unter anderem erscheinen:

- Config geladen / plausibel
- LittleFS gemountet
- History geladen
- Display initialisiert
- WLAN verbunden oder Fallback-AP
- Webserver gestartet
- MQTT verbunden, wenn konfiguriert
