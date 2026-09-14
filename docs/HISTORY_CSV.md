# History & CSV

## Deutsch
Die Langzeithistorie liegt in LittleFS und verwendet **History V3**, aktuell 32 Byte pro Tag. Gespeichert werden u. a. Datum, Füllstand/Liter, Verbrauch, Nachfüllung, Quelle, Klima-Min/Max/Mittel, Samples und CRC.

Quellen: 0=gemessen, 1=importiert, 2=Testdaten.

Kompatibler CSV-Kernheader:
```text
Datum;Fuellstand_L;Fuellstand_%25;Verbrauch_L;Nachfuellung_L;Quelle
```

Dieser Header bleibt aus Kompatibilitätsgründen auch beim englischen Build stabil.

Import akzeptiert u. a. `01.01.2026`, `1.1.26`, `1-1-26`. Zweistellige Jahre werden als 2000+YY interpretiert; zukünftige/zu alte Tage werden verworfen; bei doppelten Tagen gewinnt der letzte Datensatz; Vorschau vor Übernahme.

Wartung: Duplicate-Day-Guard, Integritätsprüfung, Reparatur, Kompaktierung, Löschen von Test-/Importdaten und Testdatenerzeugung.

## English
Long-term history is stored in LittleFS using **History V3**, currently 32 bytes/day. Stored data includes date, level/liters, consumption, refill, source, climate min/max/average, sample count and CRC.

Sources: 0=measured, 1=imported, 2=test data.

Compatible CSV core header:
```text
Datum;Fuellstand_L;Fuellstand_%25;Verbrauch_L;Nachfuellung_L;Quelle
```

This header intentionally remains stable for compatibility even in the English build.

Import accepts examples such as `01.01.2026`, `1.1.26`, `1-1-26`. Two-digit years become 2000+YY; future/too-old dates are rejected; for duplicate days the last record wins; preview is shown before applying.

Maintenance: duplicate-day guard, integrity checks, repair, compaction, removal of test/import data, and test-data generation.
