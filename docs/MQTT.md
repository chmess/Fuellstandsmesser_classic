# MQTT

## Deutsch
| Topic | Bedeutung |
|---|---|
| `average` | aktueller Tankinhalt in Litern |
| `fuellhoehe` | gefilterter ToF-Sensorabstand in mm |

`fuellhoehe` ist **nicht** die berechnete Flüssigkeitshöhe. Weitere Werte können unter einem konfigurierbaren Basistopic veröffentlicht werden. Broker: Host/IP, Port, Benutzer, Passwort, Basistopic, Aktiv/Inaktiv.

CLI: `mqtt status`, `mqtt test`, `mqtt reconnect`.

Topicnamen und maschinenlesbare Payload-Schlüssel werden nicht mit der UI-Sprache übersetzt.

## English
| Topic | Meaning |
|---|---|
| `average` | current tank content in liters |
| `fuellhoehe` | filtered ToF sensor distance in mm |

`fuellhoehe` is **not** the calculated liquid height. Additional values can be published under a configurable base topic. Broker settings: host/IP, port, username, password, base topic, enabled/disabled.

CLI: `mqtt status`, `mqtt test`, `mqtt reconnect`.

Topic names and machine-readable payload keys are not translated with the UI language.
