#include "WifiManager.h"
#include "AppTypes.h"

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

  String ssid = "Fuellstandsmesser_classic-";
  ssid += String(ESP.getChipId(), HEX);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid.c_str());

  IPAddress ip = WiFi.softAPIP();
  dnsServer.start(53, "*", ip);

  Serial.print(F("[AP] Fallback gestartet: "));
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
  WiFi.hostname("fuellstandsmesser_classic");
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);

  Serial.print(F("[WIFI] verbinde"));

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

    Serial.println(F("[MDNS] deaktiviert - Zugriff direkt per IP"));
    return;
  }

  Serial.println(F("[WIFI] STA fehlgeschlagen -> AP"));
  startAp();
}

// -----------------------------------------------------------------------------
// WEB
