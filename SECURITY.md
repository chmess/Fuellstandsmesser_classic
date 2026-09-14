# Security

## Unterstützter Stand

Aktuell gepflegt:

- V0.12.x

## Netzwerkmodell

Die Firmware ist für den Betrieb in einem **vertrauenswürdigen lokalen Netzwerk** vorgesehen.

Die Weboberfläche besitzt derzeit keine allgemeine Benutzer-Authentifizierung. Daher:

- Gerät nicht direkt ins Internet exponieren
- Router-Portfreigaben vermeiden
- Zugriff über LAN/VPN bevorzugen
- Web OTA nur aus einem vertrauenswürdigen Netz verwenden
- MQTT-Broker nach Möglichkeit mit Authentifizierung betreiben

## Zugangsdaten

WLAN- und MQTT-Zugangsdaten werden in der Gerätekonfiguration gespeichert. Keine echten Zugangsdaten in GitHub-Issues, Logs oder Screenshots veröffentlichen.

## Sicherheitsprobleme melden

Bitte sensible Sicherheitsprobleme nicht als öffentliches Issue mit Zugangsdaten oder privaten Netzwerkinformationen veröffentlichen.

## Hardware- und Betriebssicherheit

Siehe [Sicherheit & Haftung](docs/SAFETY.md).
