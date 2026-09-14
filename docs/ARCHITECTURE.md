# Software-Architektur / Software Architecture

## Deutsch

Die Firmware ist in echte C++-Module aufgeteilt.

| Modul | Aufgabe |
|---|---|
| `AppConstants.h` | zentrale Compile-Time-Konstanten |
| `AppTypes.h` | gemeinsame Datenstrukturen / Config |
| `Language.h` | aktive Sprache auswählen |
| `languages/lang_de.h` | deutsche Benutzertexte |
| `languages/lang_en.h` | englische Benutzertexte |
| `AppRuntime` | setup, loop, CLI, Runtime |
| `ConfigI2C` | Config, Migrationen, I²C |
| `Sensors` | VL53L0X, VL53L1X, AHT10 |
| `Measurement` | Filter, Plausibilität, Tankberechnung |
| `Display` | Nokia 5110 |
| `WifiManager` | STA, Fallback-AP, Reconnect |
| `MqttDiagnostics` | MQTT und Diagnose |
| `History` | LittleFS-Historie, Import, Reparatur |
| `WebServerManager` | Web UI, APIs, Web OTA |

Datenfluss: Sensoren → Messung → Display/MQTT/History → Web/CSV/Statistik.

I18N ist Compile-Time-basiert. Benutzertexte werden übersetzt, maschinenlesbare Topics, API-Felder und persistente Formate bleiben stabil.

ESP8266-RAM ist ein zentraler Grenzwert; mDNS, ArduinoOTA und Home-Assistant MQTT Discovery sind bewusst nicht enthalten.

## English

The firmware is split into real C++ modules.

| Module | Responsibility |
|---|---|
| `AppConstants.h` | central compile-time constants |
| `AppTypes.h` | shared data structures / config |
| `Language.h` | active-language selection |
| `languages/lang_de.h` | German user-facing strings |
| `languages/lang_en.h` | English user-facing strings |
| `AppRuntime` | setup, loop, CLI, runtime |
| `ConfigI2C` | config, migrations, I²C |
| `Sensors` | VL53L0X, VL53L1X, AHT10 |
| `Measurement` | filtering, plausibility, tank calculation |
| `Display` | Nokia 5110 |
| `WifiManager` | STA, fallback AP, reconnect |
| `MqttDiagnostics` | MQTT and diagnostics |
| `History` | LittleFS history, import, repair |
| `WebServerManager` | web UI, APIs, Web OTA |

Data flow: sensors → measurement → display/MQTT/history → web/CSV/statistics.

I18N is compile-time based. User-facing strings are translated; machine-readable topics, API fields and persistent formats remain stable.

ESP8266 RAM is a primary constraint; mDNS, ArduinoOTA and Home Assistant MQTT Discovery are intentionally omitted.
