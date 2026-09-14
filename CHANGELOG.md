# Changelog

Alle nennenswerten Änderungen dieses Repository-Releases werden hier dokumentiert.

## [0.12.0] - 2026-09-14

### Added

- MIT License
- deutscher/englischer Sicherheits- und Haftungshinweis
- vollständige `.h/.cpp`-Modularisierung
- zentrale `AppConstants.h`
- GitHub-fertige Projektstruktur
- Hardware-/Verdrahtungsdokumentation
- aktuelle Screenshots der Classic-Weboberfläche
- Architektur-, MQTT-, CLI-, CSV- und Troubleshooting-Dokumentation

### Changed

- Haupt-Sketch in Arduino-kompatiblen Unterordner `firmware/Fuellstandsmesser_classic/` verschoben
- Hauptdatei auf `Fuellstandsmesser_classic.ino` vereinheitlicht
- gemeinsame Konstanten zentralisiert

### Compatibility

Unverändert beibehalten:

- MQTT `average`
- MQTT `fuellhoehe`
- Config V7
- History V3
- bestehende Mess-/Tanklogik
