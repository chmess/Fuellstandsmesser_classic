<p align="center">
  <img src="docs/images/project-header.svg" alt="Fuellstandsmesser_classic" width="100%">
</p>

# Fuellstandsmesser_classic

> **Projektname:** `Fuellstandsmesser_classic` – dieser Name wird im gesamten Repository einheitlich verwendet.

Ein kompakter, eigenständiger Fuellstandsmesser_classic für Heizöl- und andere Tanks auf Basis eines **ESP8266 D1 mini**. Die Firmware unterstützt **VL53L0X/VL53L1X ToF-Sensoren**, ein **Nokia-5110-Display**, **MQTT**, eine umfangreiche **Weboberfläche**, **Langzeit-Historie**, **CSV-Import/-Export** und **Web-OTA**.

> **Aktueller Release:** V0.12.0  
> **Status:** modularisiert und auf der realen ESP8266-D1-mini-Hardware lauffähig  
> **Sprache der Oberfläche:** Deutsch

<p align="center">
  <img src="docs/images/dashboard.png" alt="Dashboard" width="92%">
</p>

## Highlights

- ESP8266 D1 mini / LOLIN(WEMOS) D1 mini
- VL53L0X und VL53L1X, automatische oder feste Auswahl
- robuste Sensorinitialisierung und Recovery
- optionale AHT10-Klimamessung
- frei konfigurierbare Tankgeometrie
- Nokia 5110 / PCD8544
- WLAN STA mit Fallback-AP
- MQTT inkl. Legacy-Kompatibilität
- Web-Dashboard und Systemdiagnose
- Web-OTA
- LittleFS-basierte Langzeit-Historie V3
- CSV Import / Export mit Vorschau
- Nachfüll- und Verbrauchsauswertung
- Monatsvergleich und Statistik
- serielle WLAN-/MQTT-CLI
- umfangreiche Heap-, Storage- und History-Diagnose
- vollständig in echte `.h/.cpp`-Module aufgeteilt

## Hardware

### ESP8266 D1 mini

- ESP8266 Arduino Core: **3.1.2**
- Serial: **115200 Baud**
- Flash: **4 MB**
- Dateisystem: **LittleFS**

### I²C

| Gerät | Adresse | Anschluss |
|---|---:|---|
| VL53L0X / VL53L1X | `0x29` | SDA=D2 / SCL=D1 |
| AHT10 (optional) | `0x38` | SDA=D2 / SCL=D1 |

### Nokia 5110 / PCD8544

| Signal | D1-mini-Pin |
|---|---|
| CLK | D5 |
| DIN | D7 |
| DC | D4 |
| CS | D6 |
| RST | D3 |

### Anschlussplan

<p align="center">
  <img src="docs/images/wiring-diagram.svg" alt="Verdrahtungsplan" width="95%">
</p>

Ausführliche Hinweise: **[Hardware & Verdrahtung](docs/HARDWARE.md)**

## Weboberfläche

| Dashboard | Historie |
|---|---|
| <img src="docs/images/dashboard.png" width="480"> | <img src="docs/images/history.png" width="480"> |

| Einstellungen | System |
|---|---|
| <img src="docs/images/settings.png" width="480"> | <img src="docs/images/system.png" width="480"> |

Mehr dazu: **[Web UI](docs/WEB_UI.md)**

## MQTT

Die beiden Legacy-Topics bleiben bewusst kompatibel:

| Topic | Inhalt |
|---|---|
| `average` | Tankinhalt in Litern |
| `fuellhoehe` | gefilterter ToF-Sensorabstand in mm |

Zusätzlich veröffentlicht die Classic-Firmware normale Basistopics für u. a. Liter, Prozent, Distanz, Füllhöhe, Verbrauch, RSSI und – falls vorhanden – AHT10-Klimawerte.

Details: **[MQTT](docs/MQTT.md)**

## Projektstruktur

```text
Fuellstandsmesser_classic/
├─ firmware/
│  └─ Fuellstandsmesser_classic/
│     ├─ Fuellstandsmesser_classic.ino
│     ├─ AppConstants.h
│     ├─ AppTypes.h
│     ├─ AppRuntime.h/.cpp
│     ├─ ConfigI2C.h/.cpp
│     ├─ Sensors.h/.cpp
│     ├─ Measurement.h/.cpp
│     ├─ Display.h/.cpp
│     ├─ WifiManager.h/.cpp
│     ├─ MqttDiagnostics.h/.cpp
│     ├─ HistoryTypes.h
│     ├─ History.h/.cpp
│     └─ WebServerManager.h/.cpp
├─ docs/
│  ├─ HARDWARE.md
│  ├─ INSTALLATION.md
│  ├─ ARCHITECTURE.md
│  ├─ WEB_UI.md
│  ├─ MQTT.md
│  ├─ HISTORY_CSV.md
│  ├─ CLI.md
│  ├─ TROUBLESHOOTING.md
│  └─ images/
├─ CHANGELOG.md
├─ CONTRIBUTING.md
├─ SECURITY.md
└─ README.md
```

## Installation

Kurzfassung:

1. Arduino IDE installieren.
2. ESP8266 Board-Paket installieren.
3. **LOLIN(WEMOS) D1 R2 & mini / D1 mini** auswählen.
4. benötigte Libraries installieren.
5. `firmware/Fuellstandsmesser_classic/Fuellstandsmesser_classic.ino` öffnen.
6. kompilieren und flashen.
7. WLAN und MQTT über die Weboberfläche oder die serielle CLI konfigurieren.

Vollständige Anleitung: **[Installation](docs/INSTALLATION.md)**

## Benötigte Libraries

- Adafruit GFX Library
- Adafruit PCD8544 Nokia 5110 LCD library
- Adafruit VL53L0X
- Adafruit VL53L1X
- PubSubClient

Zusätzlich werden Komponenten des ESP8266-Core verwendet: WiFi, WebServer, DNSServer, EEPROM, LittleFS und Updater.

## Dokumentation

- [Hardware & Verdrahtung](docs/HARDWARE.md)
- [Installation / Build](docs/INSTALLATION.md)
- [Software-Architektur](docs/ARCHITECTURE.md)
- [Weboberfläche](docs/WEB_UI.md)
- [MQTT](docs/MQTT.md)
- [History & CSV](docs/HISTORY_CSV.md)
- [Serielle CLI](docs/CLI.md)
- [Fehlersuche](docs/TROUBLESHOOTING.md)
- [Sicherheit & Haftung](docs/SAFETY.md)
- [Changelog](CHANGELOG.md)

## RAM / Stabilität

Der ESP8266 hat ein knappes RAM-Budget. Die Firmware ist deshalb auf geringe dynamische Speicherlast optimiert. Entfernt bzw. bewusst nicht enthalten sind unter anderem:

- mDNS
- ArduinoOTA
- Home-Assistant MQTT Discovery

Web-OTA bleibt aktiv.

Bei Langzeittests sollten insbesondere **Heap Minimum**, **größter freier Block** und **Fragmentierung** beobachtet werden. Die Systemseite zeigt diese Werte direkt an.

## Sicherheit

Die Weboberfläche besitzt derzeit keine allgemeine Benutzer-Authentifizierung. Das Gerät sollte deshalb **nur in einem vertrauenswürdigen LAN** betrieben und nicht direkt aus dem Internet erreichbar gemacht werden.

Siehe: **[SECURITY.md](SECURITY.md)**

## Lizenz

Dieses Projekt steht unter der **MIT License**. Sie erlaubt Nutzung,
Änderung und Weitergabe unter Beibehaltung des Copyright- und Lizenzhinweises.

Siehe **[LICENSE](LICENSE)**.

## Haftung / Sicherheit

Dieses Projekt ist ein privates Hobbyprojekt und wird ohne Gewährleistung
bereitgestellt. Aufbau, Installation und Betrieb erfolgen auf eigene
Verantwortung und eigenes Risiko.

Es ist **kein zertifiziertes Mess-, Sicherheits-, Alarm-, Überfüll-,
Leckage- oder Überwachungssystem** und darf nicht als alleinige
Sicherheitsfunktion verwendet werden.

Ausführliche Hinweise: **[Sicherheit & Haftung](docs/SAFETY.md)**

## Release

Aktueller Release: **V0.12.0**

V0.12.0 ist der erste aufgeräumte Release nach der vollständigen Modularisierung des zuvor großen Arduino-Sketches. Die Mess-, History- und MQTT-Kompatibilitätslogik wurde dabei bewusst beibehalten.
