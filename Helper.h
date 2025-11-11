#ifndef Helper_h
#define Helper_h

#include <Arduino.h>

// ArduinoJson vs wolfSSL/HomeKit warning disabling
#ifdef NO_INLINE
#undef NO_INLINE
#endif
#include <ArduinoJson.h> // ArduinoJson v5 API (DynamicJsonBuffer/JsonVariant)
#include <FS.h>          // SPIFFS
#include <ESP8266WiFi.h> // WiFi and WiFiManager for ESP8266
#include <WiFiManager.h> // Captive portal configuration Wi‑Fi

class Helper
{
public:
  Helper();
  boolean loadconfig();
  JsonVariant getconfig();
  boolean saveconfig(JsonVariant json);
  void resetsettings(WiFiManager &wifim);

private:
  JsonVariant _config;
  String _configfile;
};

#endif
