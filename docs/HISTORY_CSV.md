# History & CSV

## History V3

Die Langzeithistorie liegt in LittleFS und verwendet aktuell **History V3**.

Recordgröße:

```text
32 Byte / Tag
```

Gespeichert werden u. a.:

- Datum
- Füllstand / Liter
- Mittel-/Min-/Max-Werte
- Verbrauch
- Nachfüllung
- Datenquelle
- Klima-Mittel/Min/Max
- Klima-Samples
- CRC

## Datenquellen

| Wert | Quelle |
|---:|---|
| 0 | gemessen |
| 1 | importiert |
| 2 | Testdaten |

## CSV

Kompatible Kernfelder:

```text
Datum;Fuellstand_L;Fuellstand_%25;Verbrauch_L;Nachfuellung_L;Quelle
```

Die Classic-Firmware kann zusätzliche Klimafelder verwenden.

## Importregeln

Unterstützte Datumsformen sind unter anderem:

- `01.01.2026`
- `1.1.26`
- `1-1-26`

Regeln:

- zweistellige Jahre → 2000 + YY
- zukünftige Tage werden verworfen
- ältere Datensätze außerhalb der kompatiblen Importgrenze werden verworfen
- bei doppelten Tagen gewinnt der letzte Datensatz
- importierte Werte dürfen vorhandene Tage überschreiben
- Vorschau vor endgültiger Übernahme

## Wartung

Die History-Seite und Wartungsfunktionen unterstützen:

- Duplicate-Day-Guard
- Integritätsprüfung
- Reparatur
- Kompaktierung
- Löschen von Testdaten
- Löschen importierter Daten
- Testdaten-Erzeugung
