# Fuellstandsmesser_classic

**Kompakter ESP8266-Tankmonitor mit ToF-Sensor, Nokia-5110-Display, Weboberfläche, MQTT, CSV und Langzeithistorie.**

> Aktueller Stand: **V0.12.3 I18N FIX4.3** · Zielboard: **ESP8266 D1 mini** · UI: **Deutsch / Englisch**

Fuellstandsmesser_classic ist die kompakte Classic-Variante des Tankmonitor-Projekts. Sie kombiniert berührungslose ToF-Messung, lokale Anzeige, Web-Dashboard, Verbrauchsauswertung, persistente Historie und MQTT auf einem kleinen ESP8266.

![Dashboard](docs/images/dashboard.jpg)

## Warum die Classic-Version?

- **Kleine Hardwarebasis:** ESP8266 D1 mini genügt.
- **Berührungslose Messung:** VL53L0X oder VL53L1X.
- **Lokale Anzeige:** Nokia 5110 / PCD8544.
- **Unabhängig:** Füllstandsanzeige funktioniert auch ohne Cloud.
- **Webbasiert:** Dashboard, Historie, Einstellungen und Systemdiagnose.
- **Datenfähig:** CSV Import/Export und persistente Tageshistorie.
- **MQTT-kompatibel:** Legacy-Topics average und fuellhoehe bleiben erhalten.
- **Mehrsprachig:** Deutsch/Englisch compile-time auswählbar.
- **ESP8266-bewusst:** RAM-schonende Architektur und modulare C++-Aufteilung.

## Funktionsübersicht

| Bereich | Funktionen |
|---|---|
| Füllstand | Abstand, Füllhöhe, %, Liter, Tankfaktor, Kalibrierung |
| ToF | VL53L0X / VL53L1X |
| Klima | optional AHT10: Temperatur, Feuchte, Taupunkt, Kondensationsreserve |
| Display | Nokia 5110 / PCD8544 |
| Historie | Tageswerte, Verbrauch, Nachfüllungen, Test-/Importdaten |
| Analyse | ½ / 1 / 5 / 10 Jahre, Monatsvergleich, Statistik |
| Daten | CSV Import/Export, Vorschau und Wartung |
| Netzwerk | WLAN STA + Fallback-AP |
| MQTT | Broker, Topics, Diagnose, Legacy-Kompatibilität |
| Update | Web-OTA |
| Diagnose | Heap, LittleFS, Sensorstatus, Web/API-Zustand |

## Hardware

| Funktion | Anschluss |
|---|---|
| I²C SDA | D2 / GPIO4 |
| I²C SCL | D1 / GPIO5 |
| ToF | 0x29 |
| AHT10 optional | 0x38 |
| Nokia CLK | D5 |
| Nokia DIN | D7 |
| Nokia DC | D4 |
| Nokia CS | D6 |
| Nokia RST | D3 |

![Verdrahtung](docs/images/wiring-diagram.svg)

## Weboberfläche

### Historie

![Historie](docs/images/history.jpg)

### Einstellungen

![Einstellungen](docs/images/settings.jpg)

### System

![System](docs/images/system.jpg)

## Dokumentation

- [Projektpräsentation](docs/PROJEKTPRAESENTATION.md)
- [Hardware & Verdrahtung](docs/HARDWARE.md)
- [Installation](docs/INSTALLATION.md)
- [Architektur](docs/ARCHITECTURE.md)
- [Web UI](docs/WEB_UI.md)
- [History & CSV](docs/HISTORY_CSV.md)
- [MQTT](docs/MQTT.md)
- [Sprachen](docs/LANGUAGE.md)
- [CLI](docs/CLI.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Sicherheit](docs/SAFETY.md)

## MQTT-Kompatibilität

| Topic | Bedeutung |
|---|---|
| average | aktueller Tankinhalt in Litern |
| fuellhoehe | gefilterter ToF-Sensorabstand in mm |

## Sicherheit

Hobby-/Entwicklungsprojekt. Kein zertifiziertes Mess-, Überfüll-, Leckage-, Sicherheits- oder Alarmsystem. Nur in einem vertrauenswürdigen LAN betreiben und nicht als alleinige Schutzfunktion verwenden.
