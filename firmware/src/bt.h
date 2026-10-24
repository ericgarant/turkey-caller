// bt: Bluetooth A2DP source. Connects to the saved speaker, remembers it, reconnects.
#pragma once
#include <Arduino.h>

namespace bt {

struct Device {
  char name[33];
  char addr[18];
  int rssi;
};

void begin();                  // start the BT stack (call before Wi-Fi)
bool connected();
const char *state();           // "idle" (not searching), "searching" (one ~25 s round per request), "connecting", "connected", "disconnecting"
void requestSearch();          // start looking for speakers (from any task)
void loop();                   // call from loop(): starts a requested search
String speakerName();          // saved speaker name ("" = none yet)
size_t devices(Device *out, size_t max);  // speakers seen while searching
void chooseSpeaker(const char *name);     // save the name, forget the last address, reboot
void forget();                            // clear the saved speaker, reboot

}  // namespace bt
