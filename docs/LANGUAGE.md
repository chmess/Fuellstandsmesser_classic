# Sprachen / Languages

## Deutsch
V0.12.3 verwendet getrennte Sprachdateien:

```text
firmware/Fuellstandsmesser_classic/
├─ Language.h
└─ languages/
   ├─ lang_de.h
   ├─ lang_en.h
   └─ README.md
```

Auswahl:
```cpp
#define APP_LANGUAGE LANGUAGE_DE
```
oder
```cpp
#define APP_LANGUAGE LANGUAGE_EN
```

Neue Sprache: `lang_en.h` kopieren, Werte übersetzen, Schlüssel beibehalten, Sprach-ID und Include-Auswahl in `Language.h` ergänzen und alle `LTXT_*`/`LHTML_*`-Schlüssel prüfen. Danach Web UI, Display, CLI, History/Import und OTA testen.

Nicht übersetzen: MQTT `average`, MQTT `fuellhoehe`, API-/JSON-Feldnamen sowie Config-/History-Binärformate.

## English
V0.12.3 uses separate language files. Select German or English in `Language.h` with `APP_LANGUAGE`.

To add another language, copy `lang_en.h`, translate values while keeping key names unchanged, add a language ID and include selection in `Language.h`, verify all `LTXT_*`/`LHTML_*` keys, then test web UI, display, CLI, history/import and OTA.

Do not translate MQTT `average`, MQTT `fuellhoehe`, API/JSON field names, or Config/History binary formats.
