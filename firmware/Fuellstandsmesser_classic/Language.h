#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------------
// Compile-time language selection
// -----------------------------------------------------------------------------
// 0 = German, 1 = English
#define LANGUAGE_DE 0
#define LANGUAGE_EN 1

#ifndef APP_LANGUAGE
#define APP_LANGUAGE LANGUAGE_DE
#endif

#if (APP_LANGUAGE != LANGUAGE_DE) && (APP_LANGUAGE != LANGUAGE_EN)
#error "APP_LANGUAGE must be LANGUAGE_DE (0) or LANGUAGE_EN (1)"
#endif

#if APP_LANGUAGE == LANGUAGE_EN
  #define APP_LANGUAGE_CODE "en"
  #define APP_LOCALE_CODE   "en-GB"
  #include "languages/lang_en.h"
#else
  #define APP_LANGUAGE_CODE "de"
  #define APP_LOCALE_CODE   "de-DE"
  #include "languages/lang_de.h"
#endif

// Legacy helper retained for existing code. New visible strings should use
// LTXT_* constants from the selected language file above.
#if APP_LANGUAGE == LANGUAGE_EN
  #define TR(de, en) F(en)
  #define TRC(de, en) en
#else
  #define TR(de, en) F(de)
  #define TRC(de, en) de
#endif