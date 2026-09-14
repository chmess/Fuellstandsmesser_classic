# Fuellstandsmesser_classic – Architektur

## Ziel

Die Firmware ist modular aufgebaut, damit Messung, Display, Historie und Weboberfläche unabhängig weiterentwickelt werden können, ohne den ESP8266 mit unnötigen dynamischen Strukturen zu belasten.

| Modul | Verantwortung |
|---|---|
| AppConstants.h | Pins, Adressen, Timing, Filterkonstanten |
| AppTypes.h | gemeinsame Typen und Config-Strukturen |
| Language.h / languages | Compile-Time-Sprachauswahl |
| AppRuntime | setup, loop, CLI, Laufzeitsteuerung |
| ConfigI2C | Config, Migrationen, I²C |
| Sensors | ToF und AHT10 |
| Measurement | Filter und Tankberechnung |
| Display | Nokia 5110 |
| WifiManager | STA, Fallback-AP und Reconnect |
| MqttDiagnostics | MQTT und Diagnose |
| History | LittleFS-Historie, Import, Reparatur |
| WebServerManager | Dashboard, APIs, Settings, OTA |

## Designprinzipien

- Messung soll nicht vom Webbrowser abhängen.
- MQTT-Ausfall darf die lokale Messung nicht stoppen.
- History- und Config-Formate bleiben bei Refactorings kompatibel.
- RAM-intensive Operationen werden vermieden oder gestreamt.
- sichtbare Texte sind sprachabhängig; maschinenlesbare APIs/Topics bleiben stabil.

## Datenpfad

    Sensoren -> Measurement -> Livezustand
                           |-> Display
                           |-> MQTT
                           |-> History
                           `-> Web/API

## Persistenz

Config-Version 7 mit Migration älterer Versionen. History wird in LittleFS geführt; Import-, Reparatur- und Wartungsfunktionen sind getrennt von der laufenden Messung.
