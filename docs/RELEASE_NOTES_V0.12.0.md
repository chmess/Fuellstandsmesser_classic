# Release Notes – V0.12.0

V0.12.0 markiert den Abschluss der großen C++-Modularisierung.

## Schwerpunkte

- monolithischen Arduino-Sketch in getrennte Module aufgeteilt
- Konstanten zentralisiert
- Sensorik, Messung, Display, WLAN, MQTT, History, Web und Runtime klar getrennt
- bestehende Runtime-Funktionalität bewusst erhalten
- GitHub-Dokumentation und Verdrahtungsplan ergänzt

## Kompatibilität

- MQTT `average` bleibt Tankinhalt in Litern
- MQTT `fuellhoehe` bleibt gefilterter Sensorabstand in mm
- Config V7 bleibt erhalten
- History V3 bleibt erhalten

## RAM

ESP8266-RAM bleibt ein zentraler Grenzwert. Die Systemseite stellt Heap-Minimum, größten freien Block und Fragmentierung dar.
