#include "bt.h"

#include <BluetoothA2DPSource.h>
#include <Preferences.h>
#include <esp32-hal-bt.h>

#include "player.h"

namespace bt {
namespace {

constexpr size_t kMaxDevices = 12;

BluetoothA2DPSource a2dp;
String speaker;  // saved speaker name, set once at boot
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
Device seen[kMaxDevices];
size_t seenCount = 0;

void remember(const char *name, esp_bd_addr_t a, int rssi) {
  char addr[18];
  snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
  portENTER_CRITICAL(&lock);
  size_t i = 0;
  while (i < seenCount && strcmp(seen[i].addr, addr) != 0) i++;
  if (i == seenCount && seenCount < kMaxDevices) seenCount++;
  if (i < seenCount) {
    strlcpy(seen[i].name, name, sizeof(seen[i].name));
    strlcpy(seen[i].addr, addr, sizeof(seen[i].addr));
    seen[i].rssi = rssi;
  }
  portEXIT_CRITICAL(&lock);
}

// Called for every audio sink found while searching. Returning true connects to it.
bool onFound(const char *name, esp_bd_addr_t addr, int rssi) {
  remember(name, addr, rssi);
  bool match = speaker.length() > 0 && speaker.equalsIgnoreCase(name);
  log_i("found speaker \"%s\" rssi %d%s", name, rssi, match ? " -> connecting" : "");
  return match;
}

void onState(esp_a2d_connection_state_t state, void *) {
  log_i("speaker %s", a2dp.to_str(state));
}

}  // namespace

void begin() {
  Preferences prefs;
  prefs.begin("bt", true);
  speaker = prefs.getString("speaker", "");
  prefs.end();

  // Start the controller in Classic-only mode ourselves: this releases the BLE controller
  // memory (~30 KB) that the default dual mode would keep. The library then finds it running.
  if (!btStartMode(BT_MODE_CLASSIC_BT)) log_e("Bluetooth controller failed to start");

  a2dp.set_local_name("TurkeyCaller");
  a2dp.set_ssid_callback(onFound);
  a2dp.set_on_connection_state_changed(onState);
  a2dp.set_data_callback_in_frames(player::fillFrames);
  // Reconnect straight to the last speaker's address; search by name only when there is none.
  a2dp.set_auto_reconnect(speaker.length() > 0);
  a2dp.start();
  log_i("Bluetooth up, saved speaker: \"%s\"", speaker.c_str());
}

bool connected() { return a2dp.is_connected(); }

const char *state() {
  switch (a2dp.get_connection_state()) {
    case ESP_A2D_CONNECTION_STATE_CONNECTED: return "connected";
    case ESP_A2D_CONNECTION_STATE_CONNECTING: return "connecting";
    case ESP_A2D_CONNECTION_STATE_DISCONNECTING: return "disconnecting";
    default: return speaker.length() ? "searching" : "no speaker chosen";
  }
}

String speakerName() { return speaker; }

size_t devices(Device *out, size_t max) {
  portENTER_CRITICAL(&lock);
  size_t n = min(max, seenCount);
  memcpy(out, seen, n * sizeof(Device));
  portEXIT_CRITICAL(&lock);
  return n;
}

void chooseSpeaker(const char *name) {
  Preferences prefs;
  prefs.begin("bt", false);
  prefs.putString("speaker", name);
  prefs.end();
  a2dp.clean_last_connection();  // so the next boot searches for the new name
  log_i("speaker set to \"%s\", rebooting", name);
  delay(300);
  ESP.restart();
}

void forget() { chooseSpeaker(""); }

}  // namespace bt
