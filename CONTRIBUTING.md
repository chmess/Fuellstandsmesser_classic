# Contributing / Mitwirken

## Deutsch

Beiträge und Fehlerberichte sind willkommen.

Vor Pull Requests:
1. Firmware möglichst mit ESP8266 Arduino Core 3.1.2 kompilieren.
2. Keine WLAN-, MQTT- oder sonstigen Zugangsdaten committen.
3. MQTT-Kompatibilitätstopics nicht ohne Abstimmung ändern.
4. Config-/History-Formate nicht stillschweigend inkompatibel ändern.
5. Bei RAM-relevanten Änderungen Heap-Minimum und größten freien Block prüfen.
6. Neue sichtbare Texte immer in **beiden** Sprachdateien ergänzen.
7. Quellcode-Kommentare vorzugsweise Englisch halten.
8. I²C-Adressen hexadezimal dokumentieren.

## English

Contributions and bug reports are welcome.

Before pull requests:
1. Preferably compile with ESP8266 Arduino Core 3.1.2.
2. Do not commit Wi-Fi, MQTT or other credentials.
3. Do not change compatibility MQTT topics without coordination.
4. Do not silently introduce incompatible Config/History formats.
5. For RAM-related changes, check minimum heap and largest free block.
6. Add new user-facing strings to **both** language files.
7. Prefer English source-code comments.
8. Document I²C addresses in hexadecimal.
