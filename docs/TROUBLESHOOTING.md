# Fehlersuche

## I²C Scan findet nichts

Erwartete Belegung:

```text
SCL = D1 / GPIO5
SDA = D2 / GPIO4
```

Erwartete Adressen:

```text
0x29  ToF
0x38  AHT10 (optional)
```

Prüfen:

- gemeinsame Masse
- Versorgung
- SDA/SCL nicht vertauscht
- Modul für 3,3-V-Logik geeignet
- Kabel / Steckverbinder

## ToF wird nicht gefunden

Serielles Log:

```text
[TOF] 0x29 nicht gefunden
```

Prüfen:

- I²C Scan
- Adresse `0x29`
- Verkabelung
- Sensortyp in den Einstellungen

## AHT10 LOST

Der AHT10 ist optional. Ohne AHT10 läuft die Füllstandsmessung weiter.

Die Firmware versucht den Sensor zyklisch erneut zu erkennen.

## Wenig Heap

Die Systemseite zeigt:

- Heap frei
- Heap Minimum
- größten freien Block
- Fragmentierung
- Low-Heap-Events

Bei ungewöhnlich kleinen Werten:

- keine unnötig vielen Browser-Tabs offenhalten
- History/API-Abfragen beobachten
- serielles Log auf Low-Heap-Meldungen prüfen
- nach Änderungen einen längeren Stabilitätstest durchführen

## MQTT verbindet nicht

CLI:

```text
mqtt status
mqtt test
mqtt reconnect
```

Prüfen:

- Broker Host / IP
- Port
- Benutzer / Passwort
- WLAN
- DNS, falls Hostname verwendet wird

## Weboberfläche nicht erreichbar

- Geräte-IP im seriellen Log prüfen.
- Bei fehlender WLAN-Verbindung nach dem Fallback-AP `Fuellstandsmesser_classic-<chipid>` suchen.
