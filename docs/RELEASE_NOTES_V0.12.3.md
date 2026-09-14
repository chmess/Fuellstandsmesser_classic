# Release Notes – V0.12.3 I18N FIX3

## Deutsch
V0.12.3 führt compile-time-basierte Internationalisierung ein.

- `Language.h` als zentrale Sprachauswahl
- `languages/lang_de.h` und `languages/lang_en.h`
- deutsche/englische Web-, Display-, CLI- und Diagnose-Texte
- History-JavaScript-Fehler aus frühem V0.12.3-I18N-Stand behoben
- browserseitigen Übersetzungsansatz aus V0.12.2 entfernt
- weitere hart codierte deutsche Texte ausgelagert
- Quellcode-Kommentare auf Englisch vereinheitlicht
- MQTT, API/JSON, Config V7 und History V3 unverändert

## English
V0.12.3 introduces compile-time internationalization.

- central language selection in `Language.h`
- `languages/lang_de.h` and `languages/lang_en.h`
- German/English web, display, CLI and diagnostic strings
- fixed the History JavaScript issue from the early V0.12.3 I18N state
- removed the browser-side translation approach from V0.12.2
- moved more hard-coded German strings into language files
- standardized source comments to English
- MQTT, API/JSON, Config V7 and History V3 unchanged

Build note: this state should be compiled and tested in both languages on real hardware before creating a release tag.
