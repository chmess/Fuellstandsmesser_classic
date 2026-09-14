# Software-Architektur / Software Architecture

## Deutsch
Die Classic-Firmware ist in echte C++-Module aufgeteilt. V0.12.3 ergänzt eine Compile-Time-Sprachschicht.

| Modul | Aufgabe |
|---|---|
| `AppConstants.h` | zentrale Compile-Time-Konstanten |
| `AppTypes.h` | gemeinsame Datenstrukturen / Config |
| `Language.h` | Auswahl der aktiven Sprache |
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

I18N: Benutzertexte werden übersetzt; Topics, API-Felder und persistente Formate bleiben stabil. ESP8266-RAM bleibt zentraler Grenzwert; mDNS, ArduinoOTA und Home-Assistant MQTT Discovery sind bewusst nicht enthalten.

## English
The Classic firmware is split into real C++ modules. V0.12.3 adds a compile-time language layer.

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

I18N: user-facing strings are translated; topics, API fields and persistent formats remain stable. ESP8266 RAM remains a primary constraint; mDNS, ArduinoOTA and Home Assistant MQTT Discovery are intentionally omitted.
