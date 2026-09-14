# Sprachdateien / Language files

## Deutsch
`lang_de.h` und `lang_en.h` enthalten die Benutzertexte der Firmware. `Language.h` wählt beim Kompilieren genau eine Sprache. Beim Hinzufügen neuer Texte müssen die gleichen Schlüssel in allen Sprachdateien vorhanden sein. Maschinenlesbare Protokollnamen sollen nicht übersetzt werden.

## English
`lang_de.h` and `lang_en.h` contain the firmware's user-facing strings. `Language.h` selects exactly one language at compile time. When adding new strings, use the same keys in every language file. Machine-readable protocol identifiers should not be translated.