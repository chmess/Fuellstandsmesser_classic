# Fuellstandsmesser_classic – Installation & Betrieb

## Zielhardware

- ESP8266 D1 mini
- 4 MB Flash
- serielle Ausgabe: 115200 Baud

## Benötigte Libraries

- ESP8266 Arduino Core
- Adafruit GFX
- Adafruit PCD8544
- Adafruit VL53L0X
- Adafruit VL53L1X
- PubSubClient

## Installation

1. Hardware nach HARDWARE.md verdrahten.
2. Arduino-Board LOLIN(WEMOS) D1 R2 & mini auswählen.
3. Projektordner firmware/Fuellstandsmesser_classic öffnen.
4. gewünschte Sprache in Language.h wählen.
5. kompilieren und flashen.
6. Serial Monitor auf 115200 Baud öffnen.
7. WLAN über Weboberfläche oder CLI konfigurieren.
8. Tankgeometrie, Maße und Leer-/Vollwerte einstellen.
9. MQTT optional konfigurieren.

## Sprache

Die Firmware wird compile-time auf Deutsch oder Englisch gebaut. Sprachdateien liegen unter firmware/Fuellstandsmesser_classic/languages.

## Wartung

- Web-OTA für Firmwareupdates
- History-CSV regelmäßig exportieren
- Systemseite auf Heap-/LittleFS-Probleme prüfen
- Sensorfenster sauber halten
- bei mechanischen Änderungen neu kalibrieren

## Netzwerk

Bei fehlender STA-Verbindung kann ein Fallback-AP für Wiederherstellung/Konfiguration genutzt werden. Die Weboberfläche besitzt keine allgemeine Benutzer-Authentifizierung und gehört in ein vertrauenswürdiges LAN.
