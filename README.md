<p align="center">
  <img src="docs/images/project-header.svg" alt="Fuellstandsmesser_classic" width="100%">
</p>

# Fuellstandsmesser_classic

[Deutsch](#deutsch) · [English](#english)

## Deutsch

`Fuellstandsmesser_classic` ist ein kompakter, eigenständiger Füllstandsmesser für Heizöl- und andere Tanks auf Basis eines **ESP8266 D1 mini**. Die Firmware unterstützt **VL53L0X/VL53L1X ToF-Sensoren**, ein **Nokia-5110-Display**, **MQTT**, eine umfangreiche **Weboberfläche**, **Langzeit-Historie**, **CSV-Import/-Export**, **Web-OTA** und seit V0.12.3 eine **Compile-Time-Sprachauswahl**.

> **Aktueller Stand:** V0.12.3 I18N FIX4.3  
> **Zielhardware:** ESP8266 D1 mini  
> **Oberflächensprachen:** Deutsch / Englisch, beim Kompilieren auswählbar

### Screenshots

#### Dashboard
<p align="center">
  <img src="docs/images/dashboard.jpg" alt="Dashboard" width="92%">
</p>

#### Historie
<p align="center">
  <img src="docs/images/history.jpg" alt="Historie" width="92%">
</p>

#### Einstellungen
<p align="center">
  <img src="docs/images/settings.jpg" alt="Einstellungen" width="92%">
</p>

#### System
<p align="center">
  <img src="docs/images/system.jpg" alt="System" width="92%">
</p>

### Dokumentation

- [Web UI](docs/WEB_UI.md)
- [Hardware & Verdrahtung / Hardware & wiring](docs/HARDWARE.md)
- [Installation & Build](docs/INSTALLATION.md)
- [Architektur / Architecture](docs/ARCHITECTURE.md)
- [Sprachen / Languages](docs/LANGUAGE.md)
- [MQTT](docs/MQTT.md)
- [History & CSV](docs/HISTORY_CSV.md)
- [Serielle CLI / Serial CLI](docs/CLI.md)
- [Fehlersuche / Troubleshooting](docs/TROUBLESHOOTING.md)
- [Sicherheit & Haftung / Safety & liability](docs/SAFETY.md)

---

## English

`Fuellstandsmesser_classic` is a compact standalone tank-level monitor for heating-oil and other tanks based on an **ESP8266 D1 mini**. The firmware supports **VL53L0X/VL53L1X ToF sensors**, a **Nokia 5110 display**, **MQTT**, a comprehensive **web interface**, **long-term history**, **CSV import/export**, **Web OTA**, and since V0.12.3 a **compile-time language selection**.

> **Current state:** V0.12.3 I18N FIX4.3  
> **Target hardware:** ESP8266 D1 mini  
> **UI languages:** German / English, selected at compile time

### Screenshots

#### Dashboard
<p align="center">
  <img src="docs/images/dashboard.jpg" alt="Dashboard" width="92%">
</p>

#### History
<p align="center">
  <img src="docs/images/history.jpg" alt="History" width="92%">
</p>

#### Settings
<p align="center">
  <img src="docs/images/settings.jpg" alt="Settings" width="92%">
</p>

#### System
<p align="center">
  <img src="docs/images/system.jpg" alt="System" width="92%">
</p>

### Documentation

- [Web UI](docs/WEB_UI.md)
- [Hardware & wiring](docs/HARDWARE.md)
- [Installation & Build](docs/INSTALLATION.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Languages](docs/LANGUAGE.md)
- [MQTT](docs/MQTT.md)
- [History & CSV](docs/HISTORY_CSV.md)
- [Serial CLI](docs/CLI.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Safety & liability](docs/SAFETY.md)


### V0.12.3 I18N FIX4.3

- Dashboard-Tankgrafik auf eine zentrale Prozentanzeige reduziert.
- überlagerte Tanktexte entfernt.
- dynamische Dashboard-Aktualisierung repariert.
- Ursache war ein JavaScript-Verweis auf das nicht definierte Objekt `I`; korrigiert auf `T` und zusätzlich abgesichert.
- keine Änderungen an MQTT-Topics, API-/JSON-Feldern, Config V7 oder History V3.

### V0.12.3 I18N FIX4.3

- Dashboard tank graphic reduced to a single centered percentage value.
- overlapping tank text removed.
- dynamic dashboard updates repaired.
- root cause was a JavaScript reference to the undefined object `I`; corrected to `T` with an additional null guard.
- no changes to MQTT topics, API/JSON fields, Config V7 or History V3.
