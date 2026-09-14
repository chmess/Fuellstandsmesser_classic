# Hardware & Verdrahtung / Hardware & Wiring

## Deutsch

> Hobbyprojekt, kein zertifiziertes Mess- oder Sicherheitssystem. Aufbau und Betrieb auf eigene Verantwortung.

| Funktion | D-Pin | GPIO |
|---|---:|---:|
| I²C SCL | D1 | GPIO5 |
| I²C SDA | D2 | GPIO4 |
| Nokia CLK | D5 | GPIO14 |
| Nokia DIN | D7 | GPIO13 |
| Nokia DC | D4 | GPIO2 |
| Nokia CS | D6 | GPIO12 |
| Nokia RST | D3 | GPIO0 |

I²C: ToF `0x29`, AHT10 optional `0x38`. Aktuelle Firmware: **SDA=D2, SCL=D1**. VL53L1X wurde auf realer Classic-Hardware verwendet; VL53L0X bleibt unterstützt.

![Verdrahtungsplan / Wiring diagram](images/wiring-diagram.svg)

Hinweise: 3,3-V-Logik, gemeinsame Masse, Modulversorgung nach Breakout-Spezifikation. ToF XSHUT/INT werden nicht benötigt. Nokia-Backlight ist kein definierter Firmware-Ausgang.

Die ToF-Messung liefert den Abstand zur Flüssigkeitsoberfläche; aus Geometrie und Kalibrierung entstehen Füllhöhe, Prozent und Liter.

## English

> Hobby project, not a certified measurement or safety system. Assembly and operation are at your own risk.

| Function | D pin | GPIO |
|---|---:|---:|
| I²C SCL | D1 | GPIO5 |
| I²C SDA | D2 | GPIO4 |
| Nokia CLK | D5 | GPIO14 |
| Nokia DIN | D7 | GPIO13 |
| Nokia DC | D4 | GPIO2 |
| Nokia CS | D6 | GPIO12 |
| Nokia RST | D3 | GPIO0 |

I²C: ToF `0x29`, optional AHT10 `0x38`. Current firmware: **SDA=D2, SCL=D1**. VL53L1X has been used on real Classic hardware; VL53L0X remains supported.

Notes: 3.3-V logic, common ground, module power according to breakout specification. ToF XSHUT/INT are not required. Nokia backlight is not assigned as a firmware output.

The ToF sensor measures distance to the liquid surface; geometry and calibration produce liquid height, percentage and liters.
