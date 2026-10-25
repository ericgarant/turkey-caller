// settings: Wi-Fi name, password and device name, kept in NVS (F05).
#pragma once
#include <Arduino.h>

namespace settings {

constexpr const char *kDefaultSsid = "TurkeyCaller";
constexpr const char *kDefaultPass = "gobble123";
constexpr const char *kDefaultName = "Turkey Caller";

void begin();             // load from NVS (call before bt::begin and web::begin)
const char *apSsid();
const char *apPass();     // never send this to a phone or the serial log
const char *deviceName();
bool defaultPass();       // the factory password is still in use
uint32_t rev();           // random non-zero at boot, +1 on every saved change

// Checks the values; on error returns a message for the page and changes nothing.
// pass == "" keeps the password. Sets *wifiChanged when the name or password changed
// (the board must restart for that), *changed when anything changed.
const char *validate(const String &ssid, const String &pass, const String &name);
void save(const String &ssid, const String &pass, const String &name, bool *changed, bool *wifiChanged);
void resetDefaults();     // the BOOT-button reset: back to the factory values (keeps speaker, volume, sounds)
void useDefaultsForThisBoot();  // the saved Wi-Fi name was refused by the radio: fall back, keep NVS

}  // namespace settings
