#include "bt.h"

#include <BluetoothA2DPSource.h>
#include <Preferences.h>
#include <esp32-hal-bt.h>
#include <esp_coexist.h>
#include <WiFi.h>

#include "player.h"

namespace bt {
namespace {

constexpr size_t kMaxDevices = 12;
// After a2dp.start() the library waits 10 s (connectable) before its first inquiry.
constexpr uint32_t kStartupMs = 11000;

bool onFound(const char *name, esp_bd_addr_t addr, int rssi);

// Logs every device the search sees, and also accepts speakers that send their name as a
// plain name property instead of in the extended inquiry data (the library skips those).
class Source : public BluetoothA2DPSource {
 public:
  // The library restarts the search forever when it finds nothing. With this set it runs one
  // round (~13 s) and stops, so Wi-Fi gets the radio back.
  void searchOnce() { is_end = true; }
  // Another round after a search ended without a match (runs on the caller's task; the GAP
  // API only queues the request for the Bluetooth task).
  void searchAgain() {
    is_end = true;
    s_a2d_state = APP_AV_STATE_DISCOVERING;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, kInquiryLen, 0);
  }
  static constexpr int kInquiryLen = 10;  // units of 1.28 s

 protected:
  void filter_inquiry_scan_result(esp_bt_gap_cb_param_t *param) override {
    uint32_t cod = 0;
    int rssi = -129;
    uint8_t *eir = nullptr;
    char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = "";
    for (int i = 0; i < param->disc_res.num_prop; i++) {
      esp_bt_gap_dev_prop_t *p = param->disc_res.prop + i;
      if (p->type == ESP_BT_GAP_DEV_PROP_COD) cod = *(uint32_t *)p->val;
      else if (p->type == ESP_BT_GAP_DEV_PROP_RSSI) rssi = *(int8_t *)p->val;
      else if (p->type == ESP_BT_GAP_DEV_PROP_EIR) eir = (uint8_t *)p->val;
      else if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
        int n = min<int>(p->len, ESP_BT_GAP_MAX_BDNAME_LEN);
        memcpy(name, p->val, n);
        name[n] = 0;
      }
    }
    uint8_t *a = param->disc_res.bda;
    bool audio = esp_bt_gap_is_valid_cod(cod) && is_valid_cod_service(cod);
    Serial.printf("saw %02x:%02x:%02x:%02x:%02x:%02x cod=0x%06x rssi=%d %s eir=%s name=\"%s\"\n", a[0], a[1],
                  a[2], a[3], a[4], a[5], (unsigned)cod, rssi, audio ? "audio" : "not-audio", eir ? "yes" : "no",
                  name);
    if (eir || !audio || !name[0]) {
      BluetoothA2DPSource::filter_inquiry_scan_result(param);
      return;
    }
    // Name without EIR: same steps the library takes for a match.
    if (onFound(name, param->disc_res.bda, rssi)) {
      s_a2d_state = APP_AV_STATE_DISCOVERED;
      memcpy(peer_bd_addr, param->disc_res.bda, ESP_BD_ADDR_LEN);
      set_last_connection(peer_bd_addr);
      esp_bt_gap_cancel_discovery();
    }
  }
};

Source a2dp;
String speaker;  // saved speaker name, set once at boot
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
Device seen[kMaxDevices];
size_t seenCount = 0;
bool started = false;               // A2DP stack started
uint32_t startedMs = 0;
volatile bool discovering = false;  // an inquiry is running
volatile bool searchRequested = false;
volatile bool restartAudio = false;  // ask the speaker to resume the stream (set from callbacks)
uint32_t seenStalls = 0;
volatile bool audioStarted = false;  // the speaker is taking the audio stream
volatile uint32_t connectedMs = 0;   // when the speaker last connected (0 = not connected)

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
  Serial.printf("found speaker \"%s\" rssi %d%s\n", name, rssi, match ? " -> connecting" : "");
  return match;
}

esp_coex_prefer_t coexNow = ESP_COEX_PREFER_BALANCE;
bool coexSet = false;

void setCoex(esp_coex_prefer_t p) {
  if (coexSet && p == coexNow) return;
  coexSet = true;
  coexNow = p;
  esp_err_t err = esp_coex_preference_set(p);
  Serial.printf("radio preference %s: %s\n",
                p == ESP_COEX_PREFER_BT ? "bluetooth" : p == ESP_COEX_PREFER_WIFI ? "wifi" : "balance",
                esp_err_to_name(err));
}

void onState(esp_a2d_connection_state_t state, void *) {
  Serial.printf("speaker %s\n", a2dp.to_str(state));
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    audioStarted = false;
    connectedMs = millis() | 1;
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    connectedMs = 0;
    player::stop();  // a repeating call ends; nothing plays by itself after a reconnect
  }
  // The radio preference itself is set in loop(), which also watches for phones.
}

void onAudio(esp_a2d_audio_state_t state, void *) {
  // Some speakers suspend the stream while staying connected; taps would then play nothing.
  if (state == ESP_A2D_AUDIO_STATE_SUSPEND && a2dp.is_connected()) restartAudio = true;
  audioStarted = state == ESP_A2D_AUDIO_STATE_STARTED;
  Serial.printf("speaker audio %s\n", state == ESP_A2D_AUDIO_STATE_STARTED ? "started"
                                        : state == ESP_A2D_AUDIO_STATE_SUSPEND ? "suspended" : "stopped");
}

void onDiscovery(esp_bt_gap_discovery_state_t st) {
  discovering = st == ESP_BT_GAP_DISCOVERY_STARTED;
  Serial.printf("speaker search %s\n", discovering ? "started" : "ended");
}

void startStack() {
  if (started) return;
  started = true;
  startedMs = millis();
  setCoex(ESP_COEX_PREFER_WIFI);
  a2dp.start();
  a2dp.searchOnce();
  Serial.printf("Bluetooth started, saved speaker: \"%s\"\n", speaker.c_str());
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
  a2dp.set_discovery_mode_callback(onDiscovery);
  a2dp.set_on_audio_state_changed(onAudio);
  a2dp.set_data_callback_in_frames(player::fillFrames);
  // Reconnect straight to the last speaker's address; search by name only when there is none.
  a2dp.set_auto_reconnect(speaker.length() > 0);
  // With no speaker saved, don't search until asked: an endless Bluetooth search starves
  // Wi-Fi of radio time and phones fail to join the access point.
  if (speaker.length() > 0) startStack();
  else Serial.println("Bluetooth idle: no speaker saved, waiting for a search request");
}

void requestSearch() { searchRequested = true; }

void loop() {
  // Wi-Fi gets the radio unless a speaker is connected AND a phone is on the access point.
  // A connected speaker streams continuously (silence too), and under BALANCE phones failed to
  // join ("Connection failed"). With nobody joined there is nothing to play, so Wi-Fi first;
  // once a phone has joined, share the radio so the audio stays smooth.
  static uint32_t lastCoexCheck = 0;
  if (millis() - lastCoexCheck >= 500) {
    lastCoexCheck = millis();
    setCoex(a2dp.is_connected() && WiFi.softAPgetStationNum() > 0 ? ESP_COEX_PREFER_BALANCE
                                                                   : ESP_COEX_PREFER_WIFI);
  }
  // Sometimes the stream never starts after a connect, and the first tap would be lost to the
  // stall check. Ask for it every 3 s until the speaker reports the audio started.
  // Gives up after 5 asks per connect, in case a speaker streams without reporting it.
  static uint32_t lastStartAsk = 0, askedFor = 0;
  static int asks = 0;
  uint32_t since = connectedMs;
  if (since != askedFor) {
    askedFor = since;
    asks = 0;
  }
  if (since && !audioStarted && asks < 5 && millis() - since > 2000 && millis() - lastStartAsk > 3000) {
    lastStartAsk = millis();
    asks++;
    Serial.println("speaker connected but its audio did not start");
    restartAudio = true;
  }
  // A clip stalled (the speaker stopped pulling audio) or the speaker suspended the stream:
  // ask it to start again. If that doesn't help, the next stall asks again.
  uint32_t st = player::stats().stalls;
  if (st != seenStalls) {
    seenStalls = st;
    restartAudio = true;
  }
  if (restartAudio && a2dp.is_connected()) {
    restartAudio = false;
    esp_err_t err = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    Serial.printf("asked the speaker to restart the audio stream: %s\n", esp_err_to_name(err));
  }
  if (!searchRequested) return;
  searchRequested = false;
  if (!started) {
    startStack();
  } else if (!discovering && !a2dp.is_connected() && millis() - startedMs > kStartupMs) {
    Serial.println("searching again");
    a2dp.searchAgain();
  }
}

bool connected() { return a2dp.is_connected(); }

const char *state() {
  switch (a2dp.get_connection_state()) {
    case ESP_A2D_CONNECTION_STATE_CONNECTED: return "connected";
    case ESP_A2D_CONNECTION_STATE_CONNECTING: return "connecting";
    case ESP_A2D_CONNECTION_STATE_DISCONNECTING: return "disconnecting";
    default:
      if (started && (discovering || millis() - startedMs < kStartupMs)) return "searching";
      return "idle";
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
  Serial.printf("speaker set to \"%s\", rebooting\n", name);
  delay(300);
  ESP.restart();
}

void forget() { chooseSpeaker(""); }

}  // namespace bt
