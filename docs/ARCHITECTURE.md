# Software-Architektur

V0.12.0 ist der erste Release nach der vollständigen Aufteilung des ursprünglich großen Arduino-Sketches in echte C++-Module.

## Module

| Modul | Aufgabe |
|---|---|
| `AppConstants.h` | zentrale Compile-Time-Konstanten |
| `AppTypes.h` | gemeinsame Datenstrukturen / Config |
| `AppRuntime` | `setup()`, `loop()`, CLI, Runtime-Steuerung |
| `ConfigI2C` | Config, EEPROM-Migrationen, I²C-Helfer |
| `Sensors` | VL53L0X, VL53L1X, AHT10 |
| `Measurement` | Filter, Plausibilität, Tankberechnung |
| `Display` | Nokia 5110 / PCD8544 |
| `WifiManager` | STA, Fallback-AP, Reconnect |
| `MqttDiagnostics` | MQTT, Heap-/Storage-Diagnose |
| `HistoryTypes` | History-Datenformate |
| `History` | LittleFS-Historie, Import, Reparatur |
| `WebServerManager` | Web UI, APIs, Web OTA |

## Datenfluss

```text
ToF / AHT10
    │
    ▼
Sensors
    │
    ▼
Measurement ──────► Display
    │
    ├──────────────► MQTT
    │
    └──────────────► History
                         │
                         ▼
                    Web / CSV / Statistik
```

## Warum modularisiert?

Ziele:

- kleinere, übersichtlichere Dateien
- klarere Abhängigkeiten
- weniger Arduino-Autoprototyp-Probleme
- gezieltere Wartung
- bessere Git-Diffs
- bessere Dokumentierbarkeit
- leichtere spätere Tests

## RAM-Strategie

ESP8266-RAM ist der wichtigste Systemgrenzwert.

Bewusste Entscheidungen:

- kein mDNS
- kein ArduinoOTA
- kein Home-Assistant MQTT Discovery
- Webantworten streamen
- reduzierte große temporäre Strings
- HTTP `Connection: close`
- MQTT-Puffer begrenzt
- History-/Climate-API getrennt

Die Systemseite zeigt Heap-Minimum, größten Block und Fragmentierung.
