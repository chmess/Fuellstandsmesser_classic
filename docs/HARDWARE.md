# Hardware & Verdrahtung

> **Sicherheit:** Dieses Projekt ist ein Hobbyprojekt und kein zertifiziertes
> Mess- oder Sicherheitssystem. Aufbau und Betrieb erfolgen auf eigene
> Verantwortung. Siehe [Sicherheit & Haftung](SAFETY.md).

## Zielhardware

Die aktuelle Classic-Firmware ist für einen **ESP8266 D1 mini** ausgelegt.

### Firmware-relevante Pinbelegung

| Funktion | D-Pin | GPIO |
|---|---:|---:|
| I²C SCL | D1 | GPIO5 |
| I²C SDA | D2 | GPIO4 |
| Nokia CLK | D5 | GPIO14 |
| Nokia DIN | D7 | GPIO13 |
| Nokia DC | D4 | GPIO2 |
| Nokia CS | D6 | GPIO12 |
| Nokia RST | D3 | GPIO0 |

> Die aktuelle, getestete Firmware verwendet **SDA=D2** und **SCL=D1**.

## I²C-Geräte

| Gerät | Adresse | Status |
|---|---:|---|
| VL53L0X / VL53L1X | `0x29` | unterstützt |
| AHT10 | `0x38` | optional |

Der VL53L1X ist auf der realen Classic-Hardware bestätigt. VL53L0X bleibt softwareseitig unterstützt.

## Nokia 5110 / PCD8544

Das Display wird über fünf digitale Signale angesteuert:

- CLK → D5
- DIN → D7
- DC → D4
- CS → D6
- RST → D3

Die Displaylogik unterstützt mehrere Informationsseiten und automatischen Seitenwechsel.

## Anschlussplan

![Verdrahtungsplan](images/wiring-diagram.svg)

### Wichtige Hinweise

- ESP8266-Logikpegel: **3,3 V**
- GND aller Module muss gemeinsam verbunden sein.
- Die konkrete Spannungsversorgung eines ToF- oder Nokia-Moduls hängt vom verwendeten Breakout-Modul ab. Die Firmware selbst setzt **3,3-V-Logik** voraus.
- XSHUT/INT der ToF-Sensoren werden in dieser Firmware nicht benötigt.
- Die Nokia-5110-Hintergrundbeleuchtung ist nicht Teil der Firmware-Pinbelegung und wird daher im Plan nicht als gesteuerter Ausgang gezeigt.

## Tankgeometrie

Die Firmware unterstützt konfigurierbare Tankgeometrien. Für die Literberechnung werden die im Webinterface eingetragenen Abmessungen verwendet.

Die ToF-Messung liefert den Abstand von Sensor zu Flüssigkeitsoberfläche. Daraus werden Füllhöhe, Prozent und Liter berechnet.
