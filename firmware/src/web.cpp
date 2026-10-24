#include "web.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "bt.h"
#include "page.h"
#include "player.h"

namespace web {
namespace {

constexpr const char *kApName = "TurkeyCaller";
constexpr const char *kApPassword = "gobble123";  // Phase 0 only; becomes configurable in Phase 2
constexpr int kApChannel = 6;
constexpr int kMaxClients = 2;

AsyncWebServer server(80);
DNSServer dns;

volatile bool speakerPending = false;
char pendingSpeaker[33] = "";

// Escapes a string for a JSON value (speaker names are user-controlled).
String jsonStr(const char *s) {
  String out = "\"";
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
  return out + "\"";
}

String param(AsyncWebServerRequest *req, const char *name) {
  if (req->hasParam(name, true)) return req->getParam(name, true)->value();
  if (req->hasParam(name)) return req->getParam(name)->value();
  return "";
}

void sendOk(AsyncWebServerRequest *req) { req->send(200, "application/json", "{\"ok\":true}"); }

void sendError(AsyncWebServerRequest *req, int code, const char *msg) {
  req->send(code, "application/json", String("{\"ok\":false,\"error\":") + jsonStr(msg) + "}");
}

void handleStatus(AsyncWebServerRequest *req) {
  player::Stats p = player::stats();
  String j;
  j.reserve(400);
  j += "{\"bt\":" + jsonStr(bt::state());
  j += ",\"speaker\":" + jsonStr(bt::speakerName().c_str());
  j += ",\"playing\":";
  j += p.playing ? "true" : "false";
  j += ",\"current\":" + jsonStr(p.current);
  j += ",\"volume\":" + String(p.volume);
  j += ",\"plays\":" + String(p.plays);
  j += ",\"underruns\":" + String(p.underruns);
  j += ",\"buffered\":" + String(p.bufferedBytes);
  j += ",\"bufferSize\":" + String(p.bufferSize);
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"minHeap\":" + String(ESP.getMinFreeHeap());
  j += ",\"maxBlock\":" + String(ESP.getMaxAllocHeap());
  j += ",\"fsFree\":" + String(LittleFS.totalBytes() - LittleFS.usedBytes());
  j += ",\"clients\":" + String(WiFi.softAPgetStationNum());
  j += ",\"uptime\":" + String(millis() / 1000);
  j += "}";
  req->send(200, "application/json", j);
}

void handleSounds(AsyncWebServerRequest *req) {
  String j = "[";
  File dir = LittleFS.open("/sounds");
  bool first = true;
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String name = f.name();
      if (!name.endsWith(".mp3")) continue;
      name.remove(name.length() - 4);
      if (!first) j += ",";
      j += jsonStr(name.c_str());
      first = false;
    }
  }
  j += "]";
  req->send(200, "application/json", j);
}

void handlePlay(AsyncWebServerRequest *req) {
  if (!bt::connected()) return sendError(req, 409, "speaker not connected");
  String id = param(req, "id");
  if (!player::play(id.c_str())) return sendError(req, 400, "bad sound id");
  sendOk(req);
}

void handleVolume(AsyncWebServerRequest *req) {
  String level = param(req, "level");
  if (level.isEmpty()) return sendError(req, 400, "missing level");
  player::setVolume((uint8_t)constrain(level.toInt(), 0, 100));
  sendOk(req);
}

void handleDevices(AsyncWebServerRequest *req) {
  bt::Device list[12];
  size_t n = bt::devices(list, 12);
  String j = "[";
  for (size_t i = 0; i < n; i++) {
    if (i) j += ",";
    j += "{\"name\":" + jsonStr(list[i].name) + ",\"addr\":" + jsonStr(list[i].addr) +
         ",\"rssi\":" + String(list[i].rssi) + "}";
  }
  j += "]";
  req->send(200, "application/json", j);
}

void handleConnect(AsyncWebServerRequest *req) {
  String name = param(req, "name");
  if (name.isEmpty()) return sendError(req, 400, "missing name (use /api/bt/forget to clear)");
  if (name.length() >= sizeof(pendingSpeaker)) return sendError(req, 400, "name too long");
  strlcpy(pendingSpeaker, name.c_str(), sizeof(pendingSpeaker));
  Serial.printf("connect requested: \"%s\"\n", pendingSpeaker);
  speakerPending = true;  // reboot happens in loop(), after this reply is sent
  sendOk(req);
}

}  // namespace

void begin() {
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(kApName, kApPassword, kApChannel, 0, kMaxClients);
  IPAddress ip = WiFi.softAPIP();
  bool dnsOk = dns.start(53, "*", ip);  // captive portal: every name resolves to us
  Serial.printf("Wi-Fi AP \"%s\" %s, channel %d, ip %s, captive DNS %s\n", kApName,
                apOk ? "up" : "FAILED", WiFi.channel(), ip.toString().c_str(), dnsOk ? "up" : "FAILED");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *res = req->beginResponse(200, "text/html", kPageGz, sizeof(kPageGz));
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-store");  // phones must not keep an old page after a flash
    req->send(res);
  });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/sounds", HTTP_GET, handleSounds);
  server.on("/api/play", HTTP_POST, handlePlay);
  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *req) {
    player::stop();
    sendOk(req);
  });
  server.on("/api/volume", HTTP_POST, handleVolume);
  server.on("/api/bt/devices", HTTP_GET, handleDevices);
  server.on("/api/bt/scan", HTTP_POST, [](AsyncWebServerRequest *req) {
    Serial.println("search requested");
    bt::requestSearch();
    sendOk(req);
  });
  server.on("/api/bt/connect", HTTP_POST, handleConnect);
  server.on("/api/bt/forget", HTTP_POST, [](AsyncWebServerRequest *req) {
    Serial.println("forget requested");
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
  Serial.println("HTTP server started on port 80");
}

int clients() { return WiFi.softAPgetStationNum(); }

void loop() {
  dns.processNextRequest();
  if (speakerPending) {
    delay(200);  // let the HTTP reply go out
    bt::chooseSpeaker(pendingSpeaker);
  }
}

}  // namespace web
