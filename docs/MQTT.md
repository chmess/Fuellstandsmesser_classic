# MQTT

## Kompatibilität

Die folgenden beiden Topics bleiben kompatibel mit dem älteren Fuellstandsmesser3:

| Topic | Bedeutung |
|---|---|
| `average` | aktueller Tankinhalt in Litern |
| `fuellhoehe` | gefilterter ToF-Sensorabstand in mm |

> `fuellhoehe` ist **nicht** die berechnete Flüssigkeitshöhe.

## Weitere Classic-Topics

Die Firmware kann zusätzlich unter einem konfigurierbaren Basistopic Werte veröffentlichen, darunter:

- Liter
- Prozent
- Sensorabstand
- Füllhöhe
- Verbrauch heute
- Verbrauch 7 Tage
- Verbrauch 30 Tage
- 30-Tage-Mittel
- WLAN RSSI
- Sensorstatus
- AHT10 Temperatur
- AHT10 Luftfeuchte
- AHT10 Taupunkt
- AHT10 Kondensationsreserve

Die exakten Topicnamen werden durch die aktuelle Konfiguration bestimmt.

## Broker

Konfigurierbar über die Weboberfläche:

- Host / IP
- Port
- Benutzer
- Passwort
- Basistopic
- Aktiv / inaktiv

## Diagnose

Die serielle CLI bietet:

```text
mqtt status
mqtt test
mqtt reconnect
```

Zusätzlich zeigt die Systemseite Verbindungs- und Fehlerzähler.
