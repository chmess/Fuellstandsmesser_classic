# Fuellstandsmesser_classic – Projektpräsentation

## Konzept

Die Classic-Version bringt einen vollständigen Tankmonitor auf einen ESP8266 D1 mini. Ein ToF-Sensor misst berührungslos den Abstand zur Flüssigkeitsoberfläche. Die Firmware berechnet daraus Füllstand, Prozent und Liter, zeigt die Werte lokal und im Browser an und speichert Tagesdaten für spätere Auswertungen.

## Was das Projekt kann

### Messung
- VL53L0X und VL53L1X
- automatische Sensorauswahl
- Mittelwert- und Medianfilter
- Sprungbestätigung und Plausibilitätsprüfung
- Startup-Stabilisierung
- Tankberechnung für Quader- und Zylindergeometrie

### Dashboard
- Liter und Prozent
- Füllhöhe
- Sensorabstand
- Liter/mm-Tankfaktor
- RSSI
- Verbrauch heute, 7, 30 und 365 Tage
- WLAN-, MQTT- und ToF-Status
- Verlaufsgrafik
- optional AHT10-Klimawerte

### Historie
- persistente Tageswerte in LittleFS
- Verbrauch und Nachfüllungen
- Quellenkennzeichnung für gemessene, importierte und Testdaten
- Ansichten für ½ Jahr, 1 Jahr, 5 Jahre und 10 Jahre
- Monatsvergleich und Statistik
- CSV Import/Export
- Importvorschau
- Wartungs-, Reparatur- und Kompaktierungsfunktionen

### Display
Das Nokia-5110-Display liefert lokale Statusseiten ohne Browser. Die Displaylogik ist vom Webserver getrennt und bleibt auch bei Netzwerkproblemen nutzbar.

### MQTT
Die Kompatibilität mit bestehenden Installationen bleibt erhalten. average liefert Liter, fuellhoehe den gefilterten ToF-Sensorabstand in mm.

### Sprache
Deutsch und Englisch werden zur Compile-Zeit gewählt. Die sichtbaren Texte liegen in getrennten Sprachdateien und können später um weitere Sprachen erweitert werden.

## Vorteile

**Minimalistische Hardware:** ein D1 mini reicht für den kompletten Funktionsumfang.

**Keine Cloud-Pflicht:** Web UI und Historie laufen lokal.

**MQTT-Kompatibilität:** bestehende Topic-Namen bleiben erhalten.

**Wartbare Firmware:** klare C++-Module statt monolithischer INO-Datei.

**Datenimport:** vorhandene Füllstandsdaten können übernommen und weiter ausgewertet werden.

**Diagnose:** Heap, LittleFS, Sensoren, Netzwerk und Webzustand sind sichtbar.

## Hardware

| Bauteil | Zweck |
|---|---|
| ESP8266 D1 mini | Steuerung und WLAN |
| VL53L0X/L1X | berührungslose Distanzmessung |
| Nokia 5110 | lokale Anzeige |
| AHT10 optional | Temperatur und Feuchte |
| LittleFS | History und Arbeitsdateien |

## Datenfluss

    ToF -> Filter -> Tankberechnung -> Dashboard / Display / MQTT
                              `-> Tageshistorie -> Charts / Statistik / CSV
    AHT10 ---------------------------> Klima / Taupunkt / Reserve

## Architektur

Die Firmware ist in AppRuntime, ConfigI2C, Sensors, Measurement, Display, WifiManager, MqttDiagnostics, History und WebServerManager aufgeteilt. Gemeinsame Pins und Konstanten liegen zentral in AppConstants.h; Typen in AppTypes.h.

## Projektstand

V0.12.3 I18N FIX4.3 enthält die zweisprachige Oberfläche, die bereinigte Tankgrafik mit zentraler Prozentanzeige und die reparierte dynamische Dashboard-Aktualisierung.

## Grenzen

- ESP8266-RAM ist knapp; große dynamische Strukturen werden bewusst vermieden.
- keine allgemeine Web-Authentifizierung
- nicht eichfähig
- kein zertifiziertes Tank-Sicherheitssystem
