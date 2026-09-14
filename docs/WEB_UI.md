# Weboberfläche / Web UI

## Deutsch
### Dashboard
Füllstand Liter/Prozent, Füllhöhe, ToF-Abstand, Tankfaktor, RSSI, Verbrauch, WLAN/MQTT/ToF-Status, Diagramm und optional AHT10-Klima.

### Historie
Langzeitverlauf, Verbrauch, Nachfüll-/Quellenmarker, Monatsvergleich, Statistik, letzte Nachfüllungen, CSV Import/Export, Wartung und Testdaten.

### Einstellungen
WLAN, ToF-Sensortyp, AHT10, Tankgeometrie, Kalibrierung, Messfilter, Display und MQTT.

### System
Firmware/Gerät, Uptime, Heap, Reset-Ursache, Flash/LittleFS, WLAN/MQTT, ToF/AHT10, History, APIs, Fragmentierung, Web-Requests, Web OTA und Displaystatus.

Die UI-Sprache wird in `Language.h` beim Kompilieren ausgewählt. API-Pfade und maschinenlesbare Feldnamen bleiben unverändert.

Wichtige Endpunkte: `/`, `/settings`, `/systemstatus`, `/update`, `/history`, `/api/status`, `/api/health`, `/api/history`, `/api/history/climate`, `/api/monthly-comparison`, `/api/recent-refills`, `/history.csv`, `/history/import`, `/history/maintenance`, `/reboot`.

## English
### Dashboard
Level in liters/percent, liquid height, ToF distance, tank factor, RSSI, consumption, Wi-Fi/MQTT/ToF state, chart and optional AHT10 climate.

### History
Long-term history, consumption, refill/source markers, monthly comparison, statistics, recent refills, CSV import/export, maintenance and test data.

### Settings
Wi-Fi, ToF sensor type, AHT10, tank geometry, calibration, measurement filter, display and MQTT.

### System
Firmware/device, uptime, heap, reset reason, flash/LittleFS, Wi-Fi/MQTT, ToF/AHT10, history, APIs, fragmentation, web requests, Web OTA and display state.

The UI language is selected at compile time in `Language.h`. API paths and machine-readable field names remain unchanged.
