#include "web.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "bt.h"
#include "heapmon.h"
#include "library.h"
#include "player.h"
#include "version.h"
#include "web_page.h"

namespace web {
namespace {

constexpr const char *kApName = "TurkeyCaller";
constexpr const char *kApPassword = "gobble123";  // becomes a setting with F05
constexpr int kApChannel = 6;
constexpr int kMaxClients = 2;
constexpr size_t kMaxSockets = 3;            // two phones plus one stale socket being replaced
constexpr uint32_t kPushPlayingMs = 2000;    // status push while a call plays
constexpr uint32_t kPushIdleMs = 10000;      // status push otherwise
// HTTP requests served at once. Each open connection holds lwIP and Wi-Fi buffers; a page load
// on two phones at once (plus captive-portal probes) dipped the heap under 40 KB. The page
// loads one thing at a time and retries a 503, so 4 covers two phones.
constexpr int kMaxRequests = 4;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dns;

char etag[12] = "";                 // hash of the built-in page, sent as ETag so phones revalidate after a reflash
volatile bool pushNow = false;       // something changed: push status on the next loop()
volatile bool repeatOn = false;      // the Repeat switch, shared by every page (off after a restart)
volatile int inFlight = 0;           // HTTP requests being served (AsyncTCP task; reset by loop())
volatile uint32_t lastRequestMs = 0;  // last time a request was admitted or finished (watchdog for a stuck inFlight)

// Escapes a string for a JSON value (speaker names are user-controlled).
void jsonStr(String &out, const char *s) {
  out += '"';
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out += c;
    }
  }
  out += '"';
}

String param(AsyncWebServerRequest *req, const char *name) {
  if (req->hasParam(name, true)) return req->getParam(name, true)->value();
  if (req->hasParam(name)) return req->getParam(name)->value();
  return "";
}

void sendOk(AsyncWebServerRequest *req) { req->send(200, "application/json", "{\"ok\":true}"); }

void sendError(AsyncWebServerRequest *req, int code, const char *msg) {
  String j = "{\"ok\":false,\"error\":";
  jsonStr(j, msg);
  j += "}";
  req->send(code, "application/json", j);
}

// With the heap nearly gone, a failed allocation inside the web server aborts the whole
// firmware. Turn requests away early instead; the page simply retries.
bool lowMemory(AsyncWebServerRequest *req) {
  if (ESP.getMaxAllocHeap() >= 6144) return false;
  Serial.printf("503 %s: low memory\n", req->url().c_str());
  req->send(503);
  return true;
}

// The page knows four states; "disconnecting" is brief and on the way to a reconnect.
const char *btState() {
  const char *s = bt::state();
  return strcmp(s, "disconnecting") == 0 ? "connecting" : s;
}

String statusJson() {
  player::Stats p = player::stats();
  String j;
  j.reserve(460);
  j += "{\"bt\":\"";
  j += btState();
  j += "\",\"speaker\":";
  jsonStr(j, bt::speakerName().c_str());
  j += ",\"stream\":\"";
  j += bt::stream();
  j += "\",\"error\":";
  jsonStr(j, bt::error().c_str());
  j += ",\"playing\":";
  j += p.playing ? "true" : "false";
  j += ",\"current\":";
  jsonStr(j, p.current);
  j += ",\"loop\":";
  j += p.playing && p.loop ? "true" : "false";
  j += ",\"repeat\":";
  j += repeatOn ? "true" : "false";
  j += ",\"pos_ms\":" + String(p.posMs);
  j += ",\"len_ms\":" + String(p.lenMs);
  j += ",\"volume\":" + String(p.volume);
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"minHeap\":" + String(heapmon::minEver());
  j += ",\"maxBlock\":" + String(ESP.getMaxAllocHeap());
  j += ",\"fsFree\":" + String(library::fsFree());
  j += ",\"fsTotal\":" + String(library::fsTotal());
  j += ",\"clients\":" + String(WiFi.softAPgetStationNum());
  j += ",\"uptime\":" + String(millis() / 1000);
  j += ",\"plays\":" + String(p.plays);
  j += ",\"underruns\":" + String(p.underruns);
  j += ",\"stalls\":" + String(p.stalls);
  j += ",\"version\":\"" FW_VERSION "\"}";
  return j;
}

void handleStatus(AsyncWebServerRequest *req) {
  if (lowMemory(req)) return;
  req->send(200, "application/json", statusJson());
}

void handleSounds(AsyncWebServerRequest *req) {
  if (lowMemory(req)) return;
  String j;
  j.reserve(library::count() * 90 + 2);
  j += '[';
  library::Sound s;
  for (size_t i = 0; library::get(i, s); i++) {
    if (i) j += ',';
    j += "{\"id\":\"";
    j += s.id;  // validated a-z0-9_
    j += "\",\"name\":";
    jsonStr(j, s.name);
    j += ",\"group\":\"";
    j += s.group;
    j += "\",\"bytes\":" + String(s.bytes) + ",\"ms\":" + String(s.ms) + "}";
  }
  j += ']';
  req->send(200, "application/json", j);
}

bool truthy(const String &v) { return v == "1" || v == "true"; }

void handlePlay(AsyncWebServerRequest *req) {
  String id = param(req, "id");
  if (!library::has(id.c_str())) return sendError(req, 400, "unknown sound");
  String loopParam = param(req, "loop");
  bool loop = loopParam.isEmpty() ? (bool)repeatOn : truthy(loopParam);
  // Never waits (F07): 200 playing now, 202 waking the speaker first, 409 no speaker.
  switch (bt::requestPlay(id.c_str(), loop)) {
    case bt::kNoSpeaker:
      Serial.println("tap refused: no speaker saved");
      return sendError(req, 409, "No speaker connected");
    case bt::kSearching:
      Serial.println("tap refused: searching for speakers");
      return sendError(req, 409, "Searching for speakers, try again after the search");
    case bt::kWaking:
      repeatOn = loop;
      pushNow = true;
      Serial.printf("tap %s%s: waking the speaker\n", id.c_str(), loop ? " (repeat)" : "");
      return req->send(202, "application/json", "{\"ok\":true,\"waking\":true}");
    case bt::kPlaying:
      break;
  }
  repeatOn = loop;  // a play with loop= also sets the shared switch
  Serial.printf("tap %s%s\n", id.c_str(), loop ? " (repeat)" : "");
  sendOk(req);
}

void setVolume(long level) { player::setVolume((uint8_t)constrain(level, 0, 100)); }

void handleVolume(AsyncWebServerRequest *req) {
  String level = param(req, "level");
  if (level.isEmpty()) return sendError(req, 400, "missing level");
  setVolume(level.toInt());
  sendOk(req);
}

void handleDevices(AsyncWebServerRequest *req) {
  if (lowMemory(req)) return;
  bt::Device list[12];
  size_t n = bt::devices(list, 12);
  String j;
  j.reserve(40 + n * 80);
  j += "{\"scanning\":";
  j += strcmp(bt::state(), "searching") == 0 ? "true" : "false";
  j += ",\"devices\":[";
  for (size_t i = 0; i < n; i++) {
    if (i) j += ',';
    j += "{\"name\":";
    jsonStr(j, list[i].name);
    j += ",\"addr\":";
    jsonStr(j, list[i].addr);
    j += ",\"rssi\":" + String(list[i].rssi) + "}";
  }
  j += "]}";
  req->send(200, "application/json", j);
}

void handleConnect(AsyncWebServerRequest *req) {
  // F06: saves the speaker and connects by address without a restart. A speaker with no name
  // is selectable by address; one with no address (rare) is found by name after a restart.
  String name = param(req, "name"), addr = param(req, "addr");
  if (name.isEmpty() && addr.isEmpty()) return sendError(req, 400, "missing addr (use /api/bt/forget to clear)");
  unsigned b[6];
  char tail;
  if (!addr.isEmpty() && (addr.length() != 17 || sscanf(addr.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x%c", &b[0], &b[1], &b[2],
                                                         &b[3], &b[4], &b[5], &tail) != 6))
    return sendError(req, 400, "addr must look like aa:bb:cc:dd:ee:ff");
  if (name.length() > 32) return sendError(req, 400, "name too long");
  if (name.isEmpty()) name = addr;  // shown in the header until the speaker says its name
  Serial.printf("connect requested: \"%s\" %s\n", name.c_str(), addr.c_str());
  bt::use(name.c_str(), addr.c_str());  // carried out by bt::loop(), after this reply is sent
  pushNow = true;
  sendOk(req);
}

void notYet(AsyncWebServerRequest *req) { sendError(req, 501, "not available in this firmware yet"); }

// ---- WebSocket ----


// Commands from the page: {"cmd":"volume","level":N}, also {"cmd":"stop"}.
void setRepeat(bool on) {
  repeatOn = on;
  player::setLoop(on);
  Serial.printf("repeat %s heap=%u low=%u\n", on ? "on" : "off", (unsigned)ESP.getFreeHeap(), (unsigned)heapmon::lowSince());
  pushNow = true;
}

void logStop() { Serial.printf("stop heap=%u low=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)heapmon::lowSince()); }

void onCommand(const char *msg) {
  if (strstr(msg, "\"cmd\":\"volume\"")) {
    const char *l = strstr(msg, "\"level\":");
    if (l) {
      setVolume(atol(l + 8));
      pushNow = true;
    }
  } else if (strstr(msg, "\"cmd\":\"stop\"")) {
    logStop();
    bt::cancelWake();
    player::stop();
    pushNow = true;
  } else if (strstr(msg, "\"cmd\":\"repeat\"")) {
    const char *o = strstr(msg, "\"on\":");
    if (o) setRepeat(o[5] == 't' || o[5] == '1' || (o[5] == '"' && o[6] == '1'));
  }
}

void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
               size_t len) {
  if (type == WS_EVT_CONNECT) {
    if (ws.count() > kMaxSockets) {
      Serial.println("ws: too many sockets, refusing one");
      client->close();
      return;
    }
    Serial.printf("ws open #%u (%u sockets) heap=%u low=%u\n", (unsigned)client->id(), (unsigned)ws.count(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)heapmon::lowSince());
    pushNow = true;  // the new page wants status straight away
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    // Commands are tiny: take only whole single-frame text messages.
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT && len < 96) {
      char msg[96];
      memcpy(msg, data, len);
      msg[len] = 0;
      onCommand(msg);
    }
  }
}

// What the page shows prominently; a change here is pushed at once.
struct Shown {
  char bt[16];
  char current[32];
  char stream[12];
  uint8_t errorLen;
  bool playing, loop, repeat;
  uint8_t volume;
  int clients;
  size_t speakerLen;
  bool operator!=(const Shown &o) const {
    return strcmp(bt, o.bt) || strcmp(current, o.current) || strcmp(stream, o.stream) || errorLen != o.errorLen || playing != o.playing || loop != o.loop || repeat != o.repeat ||
           volume != o.volume || clients != o.clients || speakerLen != o.speakerLen;
  }
};

Shown shownNow() {
  player::Stats p = player::stats();
  Shown s{};
  strlcpy(s.bt, btState(), sizeof(s.bt));
  strlcpy(s.current, p.current, sizeof(s.current));
  strlcpy(s.stream, bt::stream(), sizeof(s.stream));
  s.errorLen = (uint8_t)bt::error().length();
  s.playing = p.playing;
  s.loop = p.loop;
  s.volume = p.volume;
  s.repeat = repeatOn;
  s.clients = WiFi.softAPgetStationNum();
  s.speakerLen = bt::speakerName().length();
  return s;
}

void pushLoop() {
  static uint32_t lastCheck = 0, lastPush = 0, lastCleanup = 0;
  static Shown last{};
  uint32_t now = millis();
  if (now - lastCleanup >= 1000) {
    lastCleanup = now;
    ws.cleanupClients(kMaxSockets);
  }
  if (now - lastCheck < 100) return;
  lastCheck = now;
  if (ws.count() == 0) return;
  Shown s = shownNow();
  uint32_t every = s.playing ? kPushPlayingMs : kPushIdleMs;
  if (!pushNow && !(s != last) && now - lastPush < every) return;
  if (bt::busy() || ESP.getMaxAllocHeap() < 6144) return;  // try again shortly
  pushNow = false;
  last = s;
  lastPush = now;
  // Each push is a full snapshot, so a phone that hasn't taken the last one (screen off, weak
  // signal) gets nothing new: queued snapshots used to pile up, ~0.5 KB each, up to 32 per phone.
  String j = statusJson();
  for (auto &c : ws.getClients()) {
    if (c.status() == WS_CONNECTED && c.queueLen() == 0) c.text(j);
  }
}

// FNV-1a over the gzipped page.
void makeEtag() {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < kWebPageGzLen; i++) h = (h ^ kWebPageGz[i]) * 16777619u;
  snprintf(etag, sizeof(etag), "\"%08x\"", (unsigned)h);
}

void handlePage(AsyncWebServerRequest *req) {
  if (lowMemory(req)) return;
  Serial.printf("GET / heap=%u block=%u low=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                (unsigned)heapmon::lowSince());
  if (req->hasHeader("If-None-Match") && req->header("If-None-Match") == etag) {
    AsyncWebServerResponse *res = req->beginResponse(304);
    res->addHeader("ETag", etag);
    res->addHeader("Cache-Control", "no-cache");
    return req->send(res);
  }
  AsyncWebServerResponse *res = req->beginResponse(200, "text/html", kWebPageGz, kWebPageGzLen);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "no-cache");  // revalidate each load, so a reflashed page shows up
  res->addHeader("ETag", etag);
  req->send(res);
}

}  // namespace

void begin() {
  makeEtag();
  // Log phones joining and leaving, with the reason, to diagnose "Connection failed" on a phone.
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
      const uint8_t *m = info.wifi_ap_staconnected.mac;
      Serial.printf("phone joined %02x:%02x:%02x:%02x:%02x:%02x heap=%u low=%u\n", m[0], m[1], m[2], m[3], m[4], m[5],
                    (unsigned)ESP.getFreeHeap(), (unsigned)heapmon::lowSince());
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
      const uint8_t *m = info.wifi_ap_stadisconnected.mac;
      Serial.printf("phone left %02x:%02x:%02x:%02x:%02x:%02x reason %u\n", m[0], m[1], m[2], m[3], m[4], m[5],
                    (unsigned)info.wifi_ap_stadisconnected.reason);
    } else if (event == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) {
      Serial.printf("phone got ip %s heap=%u low=%u\n", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str(),
                    (unsigned)ESP.getFreeHeap(), (unsigned)heapmon::lowSince());
    }
  });
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(kApName, kApPassword, kApChannel, 0, kMaxClients);
  IPAddress ip = WiFi.softAPIP();
  bool dnsOk = dns.start(53, "*", ip);  // captive portal: every name resolves to us
  Serial.printf("Wi-Fi AP \"%s\" %s, channel %d, ip %s, captive DNS %s\n", kApName,
                apOk ? "up" : "FAILED", WiFi.channel(), ip.toString().c_str(), dnsOk ? "up" : "FAILED");

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  // Cap requests in flight. The socket is left out: its connection is handed over to the
  // WebSocket, so the request's disconnect callback would never run.
  // The count relies on each request's single onDisconnect slot: other code must not set
  // onDisconnect on an HTTP request (or must call inFlight-- itself). loop() resets a stuck count.
  server.addMiddleware([](AsyncWebServerRequest *req, ArMiddlewareNext next) {
    if (req->url() == "/ws") return next();
    // While the speaker connects and starts its stream (a few seconds, ~35 KB of heap), only
    // play and stop are served; page loads and the rest get 503 and the page retries.
    const String &u = req->url();
    bool busy = bt::busy() && u != "/api/play" && u != "/api/stop";
    if (busy || inFlight >= kMaxRequests) {
      Serial.printf("503 %s: busy\n", req->url().c_str());
      AsyncWebServerResponse *res = req->beginResponse(503, "application/json", "{\"ok\":false,\"error\":\"busy\"}");
      res->addHeader("Retry-After", "1");
      return req->send(res);
    }
    // Only admitted requests move the watchdog's clock: a 503 must not, or a page that keeps
    // retrying would hold off the reset of a leaked count forever.
    lastRequestMs = millis();
    inFlight = inFlight + 1;
    req->onDisconnect([] {
      if (inFlight > 0) inFlight = inFlight - 1;  // after a watchdog reset, a late disconnect must not go below 0
      lastRequestMs = millis();
    });
    next();
  });
  // No icon: without this the browser's /favicon.ico would be redirected and fetch the page again.
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *req) { req->send(204); });
  server.on("/", HTTP_GET, handlePage);
  server.on("/api/status", HTTP_GET, handleStatus);
  // on() also matches sub-paths and the first match wins, so /api/sounds/delete goes first.
  server.on("/api/sounds/delete", HTTP_POST, notYet);  // F03
  server.on("/api/sounds", HTTP_GET, handleSounds);
  server.on("/api/sounds", HTTP_POST, notYet);         // upload: F03
  server.on("/api/config", HTTP_ANY, notYet);          // F05
  server.on("/api/play", HTTP_POST, handlePlay);
  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *req) {
    logStop();
    bt::cancelWake();
    player::stop();
    sendOk(req);
  });
  server.on("/api/volume", HTTP_POST, handleVolume);
  server.on("/api/repeat", HTTP_POST, [](AsyncWebServerRequest *req) {
    String on = param(req, "on");
    if (on.isEmpty()) return sendError(req, 400, "missing on");
    setRepeat(truthy(on));
    sendOk(req);
  });
  server.on("/api/bt/devices", HTTP_GET, handleDevices);
  server.on("/api/bt/scan", HTTP_POST, [](AsyncWebServerRequest *req) {
    Serial.println("search requested");
    player::stop();  // searching takes the radio; a call would stutter
    bt::requestSearch();
    sendOk(req);
  });
  server.on("/api/bt/connect", HTTP_POST, handleConnect);
  server.on("/api/bt/forget", HTTP_POST, [](AsyncWebServerRequest *req) {
    Serial.println("forget requested");
    bt::forget();  // disconnects and clears it in bt::loop(), no restart (F06)
    pushNow = true;
    sendOk(req);
  });
  // Captive portal: phones probe random URLs; send them all to the page.
  server.onNotFound([](AsyncWebServerRequest *req) {
    // Logged with the heap to see what a phone's join-time probes cost (F10).
    Serial.printf("probe %s%s heap=%u low=%u\n", req->host().c_str(), req->url().c_str(), (unsigned)ESP.getFreeHeap(),
                  (unsigned)heapmon::lowSince());
    if (req->url().startsWith("/api/")) return sendError(req, 404, "not found");
    req->redirect("http://" + WiFi.softAPIP().toString() + "/");
  });
  server.begin();
  Serial.printf("HTTP server started on port 80, page %u bytes gzipped, firmware %s\n", (unsigned)kWebPageGzLen,
                FW_VERSION);
}

int clients() { return WiFi.softAPgetStationNum(); }

int requestsInFlight() { return inFlight; }

void loop() {
  dns.processNextRequest();
  // Insurance: no request lasts 10 s (the page is 10 KB, the server times out idle ones), so a
  // count left over after that is a leak; without this the board would answer 503 until reboot.
  if (inFlight != 0 && millis() - lastRequestMs > 10000) {
    Serial.printf("request count stuck at %d, reset\n", (int)inFlight);
    inFlight = 0;
  }
  pushLoop();
}

}  // namespace web
