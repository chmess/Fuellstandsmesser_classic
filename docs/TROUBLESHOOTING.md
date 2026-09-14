# Fehlersuche / Troubleshooting

## Deutsch
- **I²C leer:** SCL=D1/GPIO5, SDA=D2/GPIO4, ToF=`0x29`, AHT10=`0x38`; Masse, Versorgung und Verkabelung prüfen.
- **ToF fehlt:** I²C-Scan, Adresse, Verkabelung und Sensortyp prüfen.
- **AHT10 LOST:** optionaler Sensor; Füllstandsmessung läuft weiter, Recovery erfolgt zyklisch.
- **Wenig Heap:** Systemseite auf freien Heap, Minimum, größten Block und Fragmentierung prüfen; History/API-Last beobachten.
- **MQTT:** `mqtt status`, `mqtt test`, `mqtt reconnect`; Host/IP, Port, Zugangsdaten, WLAN/DNS prüfen.
- **Web UI nicht erreichbar:** Geräte-IP im seriellen Log prüfen; ggf. Fallback-AP `Fuellstandsmesser_classic-<chipid>`.
- **Englischer Build zeigt Deutsch:** `APP_LANGUAGE LANGUAGE_EN` prüfen. Kompatibilitätsnamen wie `fuellhoehe` oder CSV-Header können absichtlich unverändert bleiben.

## English
- **Empty I²C scan:** SCL=D1/GPIO5, SDA=D2/GPIO4, ToF=`0x29`, AHT10=`0x38`; check ground, power and wiring.
- **ToF missing:** check I²C scan, address, wiring and selected sensor type.
- **AHT10 LOST:** optional sensor; level measurement continues and recovery is retried periodically.
- **Low heap:** inspect free heap, minimum, largest block and fragmentation on the system page; watch History/API load.
- **MQTT:** `mqtt status`, `mqtt test`, `mqtt reconnect`; check host/IP, port, credentials, Wi-Fi/DNS.
- **Web UI unreachable:** check device IP in serial log; if needed use fallback AP `Fuellstandsmesser_classic-<chipid>`.
- **English build still shows German:** verify `APP_LANGUAGE LANGUAGE_EN`. Compatibility identifiers such as `fuellhoehe` or the CSV header may intentionally remain unchanged.
