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

Auswahl in `Language.h`:

```cpp
#define APP_LANGUAGE LANGUAGE_DE
```

oder:

```cpp
#define APP_LANGUAGE LANGUAGE_EN
```

### Neue Sprache hinzufügen
1. `lang_en.h` kopieren, z. B. als `lang_fr.h`.
2. Werte übersetzen, Schlüsselnamen beibehalten.
3. Sprach-ID in `Language.h` ergänzen.
4. Include-Auswahl ergänzen.
5. Prüfen, dass jeder verwendete `LTXT_*`/`LHTML_*`-Schlüssel in jeder Sprache existiert.
6. Web UI, Display, CLI, History/Import und OTA testen.

Nicht übersetzen: MQTT `average`, MQTT `fuellhoehe`, API-/JSON-Feldnamen sowie Config- und History-Binärformate.

## English

V0.12.3 uses separate language files:

```text
firmware/Fuellstandsmesser_classic/
├─ Language.h
└─ languages/
   ├─ lang_de.h
   ├─ lang_en.h
   └─ README.md
```

Select in `Language.h`:

```cpp
#define APP_LANGUAGE LANGUAGE_DE
```

or:

```cpp
#define APP_LANGUAGE LANGUAGE_EN
```

### Adding another language
1. Copy `lang_en.h`, e.g. to `lang_fr.h`.
2. Translate values while keeping key names unchanged.
3. Add a language ID in `Language.h`.
4. Extend the include selection.
5. Verify every used `LTXT_*`/`LHTML_*` key exists in every language.
6. Test web UI, display, CLI, history/import and OTA.

Do not translate MQTT `average`, MQTT `fuellhoehe`, API/JSON field names, or Config/History binary formats.
