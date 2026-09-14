# Contributing

Beiträge und Fehlerberichte sind willkommen.

## Vor einem Pull Request

1. Firmware mit ESP8266 Arduino Core 3.1.2 kompilieren.
2. Keine WLAN-, MQTT- oder sonstigen Zugangsdaten committen.
3. Bestehende MQTT-Kompatibilitätstopics nicht ohne Abstimmung ändern.
4. History-/Config-Formate nicht stillschweigend inkompatibel ändern.
5. Änderungen möglichst auf ein Modul begrenzen.
6. Bei RAM-relevanten Änderungen Heap-Minimum und größten freien Block testen.

## Stil

- bestehende deutsche UI-/Logtexte beibehalten
- I²C-Adressen hexadezimal dokumentieren
- neue gemeinsame Konstanten nach Möglichkeit in `AppConstants.h`
- öffentliche Modul-Schnittstellen in den jeweiligen `.h`-Dateien deklarieren
