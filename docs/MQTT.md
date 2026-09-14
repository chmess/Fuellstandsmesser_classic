# MQTT

## Deutsch

| Topic | Bedeutung |
|---|---|
| `average` | aktueller Tankinhalt in Litern |
| `fuellhoehe` | gefilterter ToF-Sensorabstand in mm |

`fuellhoehe` ist **nicht** die berechnete Flüssigkeitshöhe.

Weitere Werte können unter einem konfigurierbaren Basistopic veröffentlicht werden: Liter, Prozent, Sensorabstand, Füllhöhe, Verbrauch, RSSI, Sensorstatus sowie AHT10 Temperatur, Luftfeuchte, Taupunkt und Kondensationsreserve.

Broker-Konfiguration: Host/IP, Port, Benutzer, Passwort, Basistopic, Aktiv/Inaktiv.

CLI:
```text
mqtt status
mqtt test
mqtt reconnect
```

Topicnamen und maschinenlesbare Payload-Schlüssel werden nicht mit der UI-Sprache übersetzt.

## English

| Topic | Meaning |
|---|---|
| `average` | current tank content in liters |
| `fuellhoehe` | filtered ToF sensor distance in mm |

`fuellhoehe` is **not** the calculated liquid height.

Additional values can be published under a configurable base topic: liters, percentage, sensor distance, liquid height, consumption, RSSI, sensor status, and AHT10 temperature, humidity, dew point and condensation reserve.

Broker configuration: host/IP, port, username, password, base topic, enabled/disabled.

CLI:
```text
mqtt status
mqtt test
mqtt reconnect
```

Topic names and machine-readable payload keys are not translated with the UI language.
