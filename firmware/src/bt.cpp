#include "bt.h"

#include <BluetoothA2DPSource.h>
#include <Preferences.h>
#include <esp32-hal-bt.h>
#include <esp_coexist.h>
#include <WiFi.h>

#include "heapmon.h"
#include "player.h"
#include "settings.h"

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

  // The library's connection state (APP_AV_STATE_*), read from other tasks.
  int linkState() const { return s_a2d_state; }
  bool unconnected() const { return s_a2d_state == APP_AV_STATE_UNCONNECTED; }
  // Reconnect now instead of at the library's next 10 s heartbeat (it connects to the saved
  // address when unconnected). Runs on the library's task like its own heartbeat.
  void kick() { a2d_app_heart_beat(nullptr); }
  // Switch to the speaker at `addr` without a restart / forget it. Both run on the library's
  // task, so they never race its state machine.
  void use(const esp_bd_addr_t addr) {
    memcpy(next_, addr, ESP_BD_ADDR_LEN);
    bt_app_work_dispatch(onCommand, kCmdUse, nullptr, 0, nullptr);
  }
  // Before start(): the address the stack connects to once it is up (also saved for next boot).
  void saveAddr(const esp_bd_addr_t addr) {
    esp_bd_addr_t a;
    memcpy(a, addr, ESP_BD_ADDR_LEN);
    set_last_connection(a);
  }
  void forgetSpeaker() { bt_app_work_dispatch(onCommand, kCmdForget, nullptr, 0, nullptr); }
  // A search that found nothing leaves the library "discovering", where its heartbeat never
  // reconnects. Go back to reconnecting to the saved address (F06: a search doesn't break it).
  bool searchLeftIdle() const { return s_a2d_state == APP_AV_STATE_DISCOVERING && !discovery_active; }
  void resumeReconnect() { bt_app_work_dispatch(onCommand, kCmdResume, nullptr, 0, nullptr); }

 protected:
  static constexpr uint16_t kCmdUse = 0x7f01, kCmdForget = 0x7f02, kCmdResume = 0x7f03;
  esp_bd_addr_t next_ = {0};
  static void onCommand(uint16_t cmd, void *);
  void run(uint16_t cmd) {
    if (cmd == kCmdUse) {
      set_auto_reconnect(true);  // also makes set_last_connection() save the address
      is_autoreconnect_allowed = true;
      is_target_status_active = true;
      bool same = memcmp(peer_bd_addr, next_, ESP_BD_ADDR_LEN) == 0;
      if (s_a2d_state == APP_AV_STATE_CONNECTED) {
        if (same) return;
        // Drop the old speaker; the next heartbeat (or kick) connects to the new address.
        esp_a2d_disconnect(peer_bd_addr);
        s_a2d_state = APP_AV_STATE_DISCONNECTING;
      }
      memcpy(peer_bd_addr, next_, ESP_BD_ADDR_LEN);
      set_last_connection(peer_bd_addr);
      if (discovery_active) {
        // The library connects to peer_bd_addr as soon as the search has stopped.
        s_a2d_state = APP_AV_STATE_DISCOVERED;
        esp_bt_gap_cancel_discovery();
      } else if (s_a2d_state != APP_AV_STATE_DISCONNECTING && s_a2d_state != APP_AV_STATE_CONNECTING) {
        esp_a2d_connect(peer_bd_addr);
      }
    } else if (cmd == kCmdResume) {
      if (s_a2d_state == APP_AV_STATE_DISCOVERING && !discovery_active && has_last_connection()) {
        memcpy(peer_bd_addr, last_connection, ESP_BD_ADDR_LEN);
        s_a2d_state = APP_AV_STATE_UNCONNECTED;
      }
    } else if (cmd == kCmdForget) {
      is_target_status_active = false;  // heartbeats no longer reconnect
      if (s_a2d_state == APP_AV_STATE_CONNECTED) {
        esp_a2d_disconnect(peer_bd_addr);
        s_a2d_state = APP_AV_STATE_DISCONNECTING;
      } else if (s_a2d_state == APP_AV_STATE_CONNECTING) {
        esp_a2d_source_disconnect(peer_bd_addr);  // the connecting handler ends up unconnected
      }
      clean_last_connection();  // before turning reconnect off, so the saved address is cleared too
      set_auto_reconnect(false);
      memset(peer_bd_addr, 0, ESP_BD_ADDR_LEN);
    }
  }

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
void Source::onCommand(uint16_t cmd, void *) { a2dp.run(cmd); }

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
char speaker[33] = "";  // saved speaker name (under `lock`: the web task reads it)
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
volatile uint32_t downSinceMs = 0;   // when the link last went down (for the retry backoff)
volatile bool suspendedByUs = false; // we paused the stream after a long idle (F07: no keep-alive)
volatile uint32_t lastAudioMs = 0;   // last time a call played (for the idle suspend)

// A tap while the stream isn't ready: wake the speaker, then play (F07). Under `lock`.
struct Pending {
  bool active;
  bool loop;
  char id[32];
  uint32_t sinceMs;
} pending{};
char lastError[40] = "";            // shown by the page once ("Speaker did not answer"); under `lock`
volatile uint32_t wakeAskedMs = 0;  // last media start asked for a wake (0 = none)

// Use/forget from the page, carried out by loop(). Under `lock`.
volatile bool useRequested = false, forgetRequested = false;
char useName[33] = "";
esp_bd_addr_t useAddr = {0};

constexpr uint32_t kWakeGiveUpMs = 8000;      // F07: then "Speaker did not answer"
constexpr uint32_t kPreRollMs = 250;          // F07: silence before a woken call, for the amp
constexpr uint32_t kIdleSuspendMs = 5 * 60 * 1000;  // F07: stop streaming silence after this idle
constexpr uint32_t kFastRetryMs = 3000;       // F06: retry cadence for the first minute down
constexpr uint32_t kSlowRetryMs = 10000;      // then the library's own 10 s heartbeat

void setSpeaker(const char *name) {
  portENTER_CRITICAL(&lock);
  strlcpy(speaker, name, sizeof(speaker));
  portEXIT_CRITICAL(&lock);
}

bool haveSpeaker() { return speaker[0] != 0; }

void setError(const char *e) {
  portENTER_CRITICAL(&lock);
  strlcpy(lastError, e, sizeof(lastError));
  portEXIT_CRITICAL(&lock);
}

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

// Called for every audio sink found while searching. Returning true connects to it: only the
// saved speaker found by name (an older board saved the name alone, with no address).
bool onFound(const char *name, esp_bd_addr_t addr, int rssi) {
  remember(name, addr, rssi);
  portENTER_CRITICAL(&lock);
  bool match = speaker[0] && strcasecmp(speaker, name) == 0;
  portEXIT_CRITICAL(&lock);
  Serial.printf("found speaker \"%s\" rssi %d%s\n", name, rssi, match ? " -> connecting" : "");
  return match;
}

volatile uint32_t tapMs = 0;  // set by notePlay() (web task), taken by loop()
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
  Serial.printf("speaker %s heap=%u low=%u\n", a2dp.to_str(state), (unsigned)ESP.getFreeHeap(), (unsigned)heapmon::lowSince());
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    audioStarted = false;
    suspendedByUs = false;
    connectedMs = millis() | 1;
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    if (connectedMs || !downSinceMs) downSinceMs = millis() | 1;  // a failed attempt keeps the old time
    connectedMs = 0;
    audioStarted = false;
    player::stop();  // a repeating call ends; nothing plays by itself after a reconnect
  }
  // The radio preference itself is set in loop(), which also watches for phones.
}

void onAudio(esp_a2d_audio_state_t state, void *) {
  // Some speakers suspend the stream while staying connected; taps would then play nothing.
  // When we paused it ourselves (long idle) it stays paused until the next tap.
  if (state == ESP_A2D_AUDIO_STATE_SUSPEND && a2dp.is_connected() && !suspendedByUs) restartAudio = true;
  audioStarted = state == ESP_A2D_AUDIO_STATE_STARTED;
  if (audioStarted) suspendedByUs = false;
  Serial.printf("speaker audio %s heap=%u low=%u\n", state == ESP_A2D_AUDIO_STATE_STARTED ? "started"
                                        : state == ESP_A2D_AUDIO_STATE_SUSPEND ? "suspended" : "stopped",
                (unsigned)ESP.getFreeHeap(), (unsigned)heapmon::lowSince());
}

void onDiscovery(esp_bt_gap_discovery_state_t st) {
  discovering = st == ESP_BT_GAP_DISCOVERY_STARTED;
  Serial.printf("speaker search %s\n", discovering ? "started" : "ended");
}

void startStack() {
  if (started) return;
  started = true;
  startedMs = millis();
  downSinceMs = startedMs | 1;
  setCoex(ESP_COEX_PREFER_WIFI);
  a2dp.start();
  a2dp.searchOnce();
  Serial.printf("Bluetooth started, saved speaker: \"%s\"\n", speaker);
}

void savePrefs(const char *name) {
  Preferences prefs;
  prefs.begin("bt", false);
  prefs.putString("speaker", name);
  prefs.end();
}

// Pending tap: wake the stream (or the link), play once it runs, or give up after 8 s.
void wakeLoop(uint32_t now) {
  bool ready = a2dp.is_connected() && audioStarted;
  // Take the pending tap and clear it in one step, so a tap arriving meanwhile isn't lost.
  portENTER_CRITICAL(&lock);
  Pending p = pending;
  bool expired = p.active && !ready && now - p.sinceMs > kWakeGiveUpMs;
  if (p.active && (ready || expired)) pending.active = false;
  portEXIT_CRITICAL(&lock);
  if (!p.active) return;
  if (ready) {
    wakeAskedMs = 0;
    tapMs = millis() | 1;  // share the radio for the call
    Serial.printf("speaker awake after %u ms, playing %s\n", (unsigned)(now - p.sinceMs), p.id);
    player::play(p.id, p.loop, kPreRollMs);
    return;
  }
  if (expired) {
    wakeAskedMs = 0;
    setError("Speaker did not answer");
    Serial.printf("speaker did not answer in %u ms, dropping %s\n", (unsigned)kWakeGiveUpMs, p.id);
    return;
  }
  if (now - wakeAskedMs < 1500) return;  // one ask at a time
  wakeAskedMs = now | 1;
  if (a2dp.is_connected()) {
    suspendedByUs = false;
    esp_err_t err = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    Serial.printf("waking the speaker's stream: %s\n", esp_err_to_name(err));
  } else if (a2dp.unconnected()) {
    Serial.println("waking the speaker: reconnecting");
    a2dp.kick();
  }
}

}  // namespace

void begin() {
  Preferences prefs;
  prefs.begin("bt", true);
  String saved = prefs.getString("speaker", "");
  prefs.end();
  setSpeaker(saved.c_str());

  // Start the controller in Classic-only mode ourselves: this releases the BLE controller
  // memory (~30 KB) that the default dual mode would keep. The library then finds it running.
  if (!btStartMode(BT_MODE_CLASSIC_BT)) log_e("Bluetooth controller failed to start");

  a2dp.set_local_name(settings::deviceName());  // F05: applies at boot
  a2dp.set_ssid_callback(onFound);
  a2dp.set_on_connection_state_changed(onState);
  a2dp.set_discovery_mode_callback(onDiscovery);
  a2dp.set_on_audio_state_changed(onAudio);
  a2dp.set_data_callback_in_frames(player::fillFrames);
  // Reconnect straight to the last speaker's address; search by name only when there is none.
  a2dp.set_auto_reconnect(haveSpeaker());
  // With no speaker saved, don't search until asked: an endless Bluetooth search starves
  // Wi-Fi of radio time and phones fail to join the access point.
  if (haveSpeaker()) startStack();
  else Serial.println("Bluetooth idle: no speaker saved, waiting for a search request");
}

void requestSearch() { searchRequested = true; }

void notePlay() { tapMs = millis() | 1; }

PlayResult requestPlay(const char *id, bool loop) {
  if (!haveSpeaker()) return kNoSpeaker;
  setError("");
  if (a2dp.is_connected() && audioStarted) {
    notePlay();
    portENTER_CRITICAL(&lock);
    pending.active = false;  // a tap while waking replaces the waiting one
    portEXIT_CRITICAL(&lock);
    player::play(id, loop);
    return kPlaying;
  }
  if (discovering) return kSearching;
  player::stop();  // nothing stale plays once the speaker is back
  portENTER_CRITICAL(&lock);
  if (!pending.active) pending.sinceMs = millis();  // a second tap keeps the first one's deadline
  pending.active = true;
  pending.loop = loop;
  strlcpy(pending.id, id, sizeof(pending.id));
  portEXIT_CRITICAL(&lock);
  return kWaking;
}

void cancelWake() {
  portENTER_CRITICAL(&lock);
  pending.active = false;
  portEXIT_CRITICAL(&lock);
}

void use(const char *name, const char *addr) {
  unsigned a[6];
  esp_bd_addr_t bda = {0};
  bool haveAddr = addr && sscanf(addr, "%x:%x:%x:%x:%x:%x", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) == 6;
  for (int i = 0; haveAddr && i < 6; i++) bda[i] = (uint8_t)a[i];
  portENTER_CRITICAL(&lock);
  strlcpy(useName, name, sizeof(useName));
  memcpy(useAddr, bda, sizeof(useAddr));
  useRequested = true;
  portEXIT_CRITICAL(&lock);
}

void forget() { forgetRequested = true; }

void loop() {
  uint32_t now = millis();
  // Wi-Fi gets the radio except while a call plays (and 2 s after, so back-to-back taps don't
  // flip it). A connected speaker streams silence all the time, and sharing the radio then cost
  // phones their join ("Connection failed" under BALANCE) and ~30 KB of heap when the first one
  // joined. A tap switches at once (notePlay), before the audio ring has filled.
  static uint32_t lastCoexCheck = 0, lastActive = 0;
  static bool everActive = false;
  uint32_t tap = tapMs;
  if (tap || now - lastCoexCheck >= 100) {
    lastCoexCheck = now;
    if (tap) tapMs = 0;
    bool playing = player::stats().playing;
    if (playing) lastAudioMs = now;
    if (tap || playing) {
      lastActive = now;  // not now | 1: on an even millis() that is in the future and the hold wraps
      everActive = true;
    }
    bool active = everActive && now - lastActive < 2000;
    setCoex(a2dp.is_connected() && active ? ESP_COEX_PREFER_BALANCE : ESP_COEX_PREFER_WIFI);
  }

  // Use a speaker from the search list (F06): saved and connected by address, no restart.
  if (useRequested) {
    portENTER_CRITICAL(&lock);
    useRequested = false;
    char name[33];
    strlcpy(name, useName, sizeof(name));
    esp_bd_addr_t bda;
    memcpy(bda, useAddr, sizeof(bda));
    portEXIT_CRITICAL(&lock);
    static const esp_bd_addr_t none = {0};
    savePrefs(name);
    setSpeaker(name);
    setError("");
    player::stop();
    if (memcmp(bda, none, sizeof(bda)) == 0) {
      // Name only (a speaker that hid its address): find it by name after a restart.
      a2dp.clean_last_connection();
      Serial.printf("speaker set to \"%s\" (no address), rebooting\n", name);
      delay(300);
      ESP.restart();
    }
    Serial.printf("speaker set to \"%s\" %02x:%02x:%02x:%02x:%02x:%02x\n", name, bda[0], bda[1], bda[2], bda[3],
                  bda[4], bda[5]);
    downSinceMs = now | 1;
    if (!started) {
      a2dp.set_auto_reconnect(true);
      a2dp.saveAddr(bda);  // the stack connects to it once it is up
      startStack();
    } else {
      a2dp.use(bda);
    }
  }
  if (forgetRequested) {
    forgetRequested = false;
    savePrefs("");
    setSpeaker("");
    setError("");
    cancelWake();
    player::stop();
    if (started) a2dp.forgetSpeaker();
    Serial.println("speaker forgotten");
  }

  wakeLoop(now);

  // Reconnect (F06): the library retries every 10 s; during the first minute down, retry every
  // 3 s. Page loads are not held off by attempts (busy() starts at an actual connect).
  static uint32_t lastKick = 0, lastResume = 0;
  if (started && haveSpeaker() && !discovering && a2dp.searchLeftIdle() && now - lastResume > 2000 &&
      now - startedMs > kStartupMs) {
    lastResume = now;
    a2dp.resumeReconnect();  // no-op when there is no saved address (then only a search finds it)
  }
  uint32_t down = downSinceMs;
  if (started && haveSpeaker() && a2dp.unconnected() && !discovering && down) {
    uint32_t every = now - down < 60000 ? kFastRetryMs : kSlowRetryMs;
    if (now - lastKick >= every && now - down >= kFastRetryMs) {
      lastKick = now;
      a2dp.kick();
    }
  }

  // No keep-alive (F07): after a long idle, pause the stream so the speaker's own power saving
  // works. The next tap wakes it.
  if (a2dp.is_connected() && audioStarted && !suspendedByUs && now - lastAudioMs > kIdleSuspendMs &&
      now - connectedMs > kIdleSuspendMs) {
    suspendedByUs = true;
    esp_err_t err = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
    Serial.printf("idle %u min: pausing the speaker's stream: %s\n", (unsigned)(kIdleSuspendMs / 60000),
                  esp_err_to_name(err));
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
  if (since && !audioStarted && !suspendedByUs && asks < 5 && now - since > 2000 && now - lastStartAsk > 3000) {
    lastStartAsk = now;
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
  } else if (!discovering && !a2dp.is_connected() && now - startedMs > kStartupMs) {
    Serial.println("searching again");
    a2dp.searchAgain();
  }
}

bool connected() { return a2dp.is_connected(); }

bool busy() {
  // A connect and the stream start take ~35 KB of heap for a few seconds. The window starts
  // when the speaker actually answers (not at each attempt, so a speaker that is off never
  // keeps the page away) and is capped at 5 s.
  uint32_t now = millis(), ced = connectedMs, wake = wakeAskedMs;
  if (ced && !audioStarted && now - ced < 5000) return true;
  return wake && !audioStarted && a2dp.is_connected() && now - wake < 3000;
}

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

const char *stream() {
  bool waiting;
  portENTER_CRITICAL(&lock);
  waiting = pending.active;
  portEXIT_CRITICAL(&lock);
  if (waiting) return "waking";
  if (!a2dp.is_connected()) return "down";
  return audioStarted ? "ready" : "suspended";
}

String error() {
  char copy[sizeof(lastError)];
  portENTER_CRITICAL(&lock);
  strlcpy(copy, lastError, sizeof(copy));
  portEXIT_CRITICAL(&lock);
  return String(copy);
}

String speakerName() {
  char copy[33];
  portENTER_CRITICAL(&lock);
  strlcpy(copy, speaker, sizeof(copy));
  portEXIT_CRITICAL(&lock);
  return String(copy);
}

size_t devices(Device *out, size_t max) {
  portENTER_CRITICAL(&lock);
  size_t n = min(max, seenCount);
  memcpy(out, seen, n * sizeof(Device));
  portEXIT_CRITICAL(&lock);
  return n;
}

}  // namespace bt
