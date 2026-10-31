#include "settings.h"

#include <Preferences.h>
#include <esp_random.h>

namespace settings {
namespace {

// Sizes include the terminating 0. Limits are in bytes (UTF-8), as the radio and NVS count them.
constexpr size_t kSsidMax = 32, kPassMin = 8, kPassMax = 63, kNameMax = 24;

bool wifiOffNow = false;
char ssid[kSsidMax + 1];
char pass[kPassMax + 1];
char name[kNameMax + 1];
volatile uint32_t revNow = 1;

bool hasControl(const String &s) {
  for (size_t i = 0; i < s.length(); i++)
    if ((uint8_t)s[i] < 0x20 || s[i] == 0x7f) return true;
  return false;
}

// WPA2 passphrases are printable ASCII.
bool printableAscii(const String &s) {
  for (size_t i = 0; i < s.length(); i++)
    if ((uint8_t)s[i] < 0x20 || (uint8_t)s[i] > 0x7e) return false;
  return true;
}

void setDefaults() {
  strlcpy(ssid, kDefaultSsid, sizeof(ssid));
  strlcpy(pass, kDefaultPass, sizeof(pass));
  strlcpy(name, kDefaultName, sizeof(name));
}

}  // namespace

void begin() {
  uint32_t r = esp_random() & 0x7fffffff;
  revNow = r ? r : 1;
  setDefaults();
  Preferences prefs;
  // Read-write so the namespace exists; read-only begin() of a missing namespace logs an NVS error (B10).
  if (!prefs.begin("cfg", false)) {
    Serial.println("settings: NVS not available, using defaults");
    return;
  }
  // isKey() first: getString() of a missing key logs a NOT_FOUND error on serial (B10).
  String s = prefs.isKey("ssid") ? prefs.getString("ssid") : String(kDefaultSsid);
  String p = prefs.isKey("pass") ? prefs.getString("pass") : String(kDefaultPass);
  String n = prefs.isKey("name") ? prefs.getString("name") : String(kDefaultName);
  wifiOffNow = prefs.isKey("wifioff") && prefs.getBool("wifioff");
  prefs.end();
  // A value that no longer passes the rules (should not happen) falls back to its default.
  if (!validate(s, p, n)) {
    strlcpy(ssid, s.c_str(), sizeof(ssid));
    strlcpy(pass, p.c_str(), sizeof(pass));
    strlcpy(name, n.c_str(), sizeof(name));
  } else {
    Serial.println("settings: saved values are invalid, using defaults");
  }
  Serial.printf("settings: Wi-Fi \"%s\", device \"%s\", %s password\n", ssid, name, defaultPass() ? "factory" : "own");
}

const char *apSsid() { return ssid; }
const char *apPass() { return pass; }
const char *deviceName() { return name; }
bool defaultPass() { return strcmp(pass, kDefaultPass) == 0; }
uint32_t rev() { return revNow; }

const char *validate(const String &s, const String &p, const String &n) {
  if (s.isEmpty() || s.length() > kSsidMax) return "Wi-Fi name must be 1 to 32 characters";
  if (hasControl(s)) return "Wi-Fi name has a character that is not allowed";
  if (!p.isEmpty()) {
    if (p.length() < kPassMin || p.length() > kPassMax) return "password must be 8 to 63 characters";
    if (!printableAscii(p)) return "password must use plain letters, digits and symbols";
  }
  if (n.isEmpty() || n.length() > kNameMax) return "device name must be 1 to 24 characters";
  if (hasControl(n)) return "device name has a character that is not allowed";
  return nullptr;
}

void save(const String &s, const String &p, const String &n, bool *changed, bool *wifiChanged) {
  bool ssidNew = s != ssid, passNew = !p.isEmpty() && p != pass, nameNew = n != name;
  *wifiChanged = ssidNew || passNew;
  *changed = *wifiChanged || nameNew;
  if (!*changed) return;
  Preferences prefs;
  if (prefs.begin("cfg", false)) {
    if (ssidNew) prefs.putString("ssid", s);
    if (passNew) prefs.putString("pass", p);
    if (nameNew) prefs.putString("name", n);
    prefs.end();
  } else {
    Serial.println("settings: NVS not available, change kept until restart only");
  }
  if (ssidNew) strlcpy(ssid, s.c_str(), sizeof(ssid));
  if (passNew) strlcpy(pass, p.c_str(), sizeof(pass));
  if (nameNew) strlcpy(name, n.c_str(), sizeof(name));
  revNow = revNow + 1;
  if (revNow == 0 || revNow > 0x7fffffff) revNow = 1;
  Serial.printf("settings saved:%s%s%s (rev %u)\n", ssidNew ? " Wi-Fi name" : "", passNew ? " password" : "",
                nameNew ? " device name" : "", (unsigned)revNow);
}

void resetDefaults() {
  Preferences prefs;
  if (prefs.begin("cfg", false)) {
    prefs.clear();  // only this namespace: the speaker ("bt") and volume ("player") stay
    prefs.end();
  }
  setDefaults();
  revNow = revNow + 1;
  if (revNow == 0 || revNow > 0x7fffffff) revNow = 1;
}

bool wifiOff() { return wifiOffNow; }

void setWifiOff(bool off) {
  Preferences prefs;
  if (prefs.begin("cfg", false)) {
    prefs.putBool("wifioff", off);
    prefs.end();
  }
  Serial.printf("settings saved: Wi-Fi %s after restart\n", off ? "off (phone-free)" : "on");
}

void useDefaultsForThisBoot() {
  strlcpy(ssid, kDefaultSsid, sizeof(ssid));
  strlcpy(pass, kDefaultPass, sizeof(pass));
}

}  // namespace settings
