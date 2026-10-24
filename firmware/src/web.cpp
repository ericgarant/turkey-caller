#include "web.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "bt.h"
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

volatile bool speakerPending = false;
char pendingSpeaker[33] = "";
char etag[12] = "";
volatile bool repeatOn = false;      // the Repeat switch, shared by every page (off after a restart)
volatile int inFlight = 0;           // HTTP requests being served (AsyncTCP task; reset by loop())
volatile uint32_t lastRequestMs = 0;  // hash of the built-in page, so a reflashed page is fetched again

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
  j.reserve(420);
  j += "{\"bt\":\"";
  j += btState();
  j += "\",\"speaker\":";
  jsonStr(j, bt::speakerName().c_str());
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
  j += ",\"minHeap\":" + String(ESP.getMinFreeHeap());
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
  if (!bt::connected()) {
    Serial.println("tap refused: speaker not connected");
    return sendError(req, 409, "No speaker connected");
  }
  String loopParam = param(req, "loop");
  bool loop = loopParam.isEmpty() ? (bool)repeatOn : truthy(loopParam);
  repeatOn = loop;  // a play with loop= also sets the shared switch
  if (!player::play(id.c_str(), loop)) return sendError(req, 400, "unknown sound");
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
  // `addr` is accepted for the page's sake; connecting by address comes with F06. Until then
  // the board saves the name, restarts and finds the speaker by name.
  String name = param(req, "name");
  if (name.isEmpty()) return sendError(req, 400, "missing name (use /api/bt/forget to clear)");
  if (name.length() >= sizeof(pendingSpeaker)) return sendError(req, 400, "name too long");
  strlcpy(pendingSpeaker, name.c_str(), sizeof(pendingSpeaker));
  Serial.printf("connect requested: \"%s\" %s\n", pendingSpeaker, param(req, "addr").c_str());
  speakerPending = true;  // restart happens in loop(), after this reply is sent
  sendOk(req);
}

void notYet(AsyncWebServerRequest *req) { sendError(req, 501, "not available in this firmware yet"); }

// ---- WebSocket ----

volatile bool pushNow = false;  // something changed: push status on the next loop()

// Commands from the page: {"cmd":"volume","level":N}, also {"cmd":"stop"}.
void setRepeat(bool on) {
  repeatOn = on;
  player::setLoop(on);
  pushNow = true;
}

void onCommand(const char *msg) {
  if (strstr(msg, "\"cmd\":\"volume\"")) {
    const char *l = strstr(msg, "\"level\":");
    if (l) {
      setVolume(atol(l + 8));
      pushNow = true;
    }
  } else if (strstr(msg, "\"cmd\":\"stop\"")) {
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
  bool playing, loop, repeat;
  uint8_t volume;
  int clients;
  size_t speakerLen;
  bool operator!=(const Shown &o) const {
    return strcmp(bt, o.bt) || strcmp(current, o.current) || playing != o.playing || loop != o.loop || repeat != o.repeat ||
           volume != o.volume || clients != o.clients || speakerLen != o.speakerLen;
  }
};

Shown shownNow() {
  player::Stats p = player::stats();
  Shown s{};
  strlcpy(s.bt, btState(), sizeof(s.bt));
  strlcpy(s.current, p.current, sizeof(s.current));
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
  if (ESP.getMaxAllocHeap() < 6144 || !ws.availableForWriteAll()) return;  // try again shortly
  pushNow = false;
  last = s;
  lastPush = now;
  ws.textAll(statusJson());
}

// FNV-1a over the gzipped page.
void makeEtag() {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < kWebPageGzLen; i++) h = (h ^ kWebPageGz[i]) * 16777619u;
  snprintf(etag, sizeof(etag), "\"%08x\"", (unsigned)h);
}

void handlePage(AsyncWebServerRequest *req) {
  if (lowMemory(req)) return;
  Serial.printf("GET / heap=%u block=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
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
      Serial.printf("phone joined %02x:%02x:%02x:%02x:%02x:%02x\n", m[0], m[1], m[2], m[3], m[4], m[5]);
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
      const uint8_t *m = info.wifi_ap_stadisconnected.mac;
      Serial.printf("phone left %02x:%02x:%02x:%02x:%02x:%02x reason %u\n", m[0], m[1], m[2], m[3], m[4], m[5],
                    (unsigned)info.wifi_ap_stadisconnected.reason);
    } else if (event == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) {
      Serial.printf("phone got ip %s\n", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
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
    lastRequestMs = millis();
    if (inFlight >= kMaxRequests) {
      Serial.printf("503 %s: busy\n", req->url().c_str());
      AsyncWebServerResponse *res = req->beginResponse(503, "application/json", "{\"ok\":false,\"error\":\"busy\"}");
      res->addHeader("Retry-After", "1");
      return req->send(res);
    }
    inFlight = inFlight + 1;
    req->onDisconnect([] { inFlight = inFlight - 1; });
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
    player::stop();
    pendingSpeaker[0] = 0;
    speakerPending = true;
    sendOk(req);
  });
  // Captive portal: phones probe random URLs; send them all to the page.
  server.onNotFound([](AsyncWebServerRequest *req) {
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
  if (speakerPending) {
    delay(200);  // let the HTTP reply go out
    bt::chooseSpeaker(pendingSpeaker);
  }
}

}  // namespace web
