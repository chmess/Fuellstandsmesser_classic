#include "WifiManager.h"
#include "AppTypes.h"
#include "Language.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>

extern Config cfg;
extern DNSServer dnsServer;

extern bool apMode;
extern uint32_t lastWifiRetryMs;
extern uint32_t wifiLostSinceMs;
extern uint32_t wifiReconnectCount;
extern uint32_t wifiReconnectErrors;

// -----------------------------------------------------------------------------
void startAp() {
  if (apMode) return;

  apMode = true;

  String ssid = "FuellstandClassic-";
  ssid += String(ESP.getChipId(), HEX);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid.c_str());

  IPAddress ip = WiFi.softAPIP();
  dnsServer.start(53, "*", ip);

  Serial.print(TR("[AP] Fallback gestartet: ","[AP] Fallback started: "));
  Serial.print(ssid);
  Serial.print(F(" @ "));
  Serial.println(ip);
}

void connectWifi() {
  if (strlen(cfg.wifiSsid) == 0) {
    startAp();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.hostname("fuellstand-classic");
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);

  Serial.print(TR("[WIFI] verbinde","[WIFI] connecting"));

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000UL) {
    delay(250);
    Serial.print('.');
    yield();
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[WIFI] IP "));
    Serial.println(WiFi.localIP());

    Serial.println(TR("[MDNS] deaktiviert - Zugriff direkt per IP","[MDNS] disabled - access directly by IP"));
    return;
  }

  Serial.println(TR("[WIFI] STA fehlgeschlagen -> AP","[WIFI] STA failed -> AP"));
  startAp();
}

// -----------------------------------------------------------------------------
// WEB
