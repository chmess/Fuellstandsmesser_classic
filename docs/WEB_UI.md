# Weboberfläche

## Dashboard

Das Dashboard zeigt:

- Füllstand in Liter und Prozent
- Füllhöhe
- ToF-Sensorabstand
- Tankfaktor
- RSSI
- Verbrauch
- WLAN/MQTT/ToF-Status
- Füllstands-/Verbrauchsgrafik
- optional AHT10-Klima

![Dashboard](images/dashboard.webp)

## Historie

- Langzeit-Füllstandsverlauf
- Verbrauch
- Nachfüllmarker
- Quellenmarker
- Monatsvergleich
- Statistik
- letzte Nachfüllungen
- CSV Import / Export
- Wartung und Testdaten

![Historie](images/history.webp)

## Einstellungen

- WLAN
- ToF-Sensortyp
- AHT10
- Tankgeometrie
- Kalibrierung
- Messfilter
- Display
- MQTT

![Einstellungen](images/settings.webp)

## System

- Firmware / Gerät
- Uptime
- Heap frei / Minimum
- Reset-Ursache
- Flash / LittleFS
- WLAN / MQTT
- ToF / AHT10
- History-Zustand
- History API
- Heap-Fragmentierung
- Web-Requests
- Web OTA
- Displaystatus

![System](images/system.webp)

## HTTP-Endpunkte

Auszug aus der aktuellen Firmware:

```text
GET  /
GET  /settings
POST /save
GET  /systemstatus
GET  /update
POST /update
GET  /history
GET  /api/status
GET  /api/health
GET  /api/history
GET  /api/history/climate
GET  /api/monthly-comparison
GET  /api/recent-refills
GET  /history.csv
GET  /history/import
POST /history/import/preview
POST /history/import/apply
POST /history/import/cancel
GET  /history/maintenance
POST /history/maintenance/compact
POST /history/maintenance/repair
POST /history/maintenance/delete-test
POST /history/maintenance/delete-imported
POST /generate-test-history
POST /generate-test-history-10y
POST /clear-history
GET  /reboot
```
