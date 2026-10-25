#include "uploads.h"

#include <freertos/semphr.h>

#include "heapmon.h"
#include "library.h"
#include "player.h"
#include "sdcard.h"
#include "wav.h"

namespace uploads {
namespace {

constexpr uint64_t kReserveBytes = 32 * 1024;  // F03: keep this much free on the card
constexpr uint32_t kMaxMs = 60000;             // F03: 60 s per clip
constexpr uint32_t kRate = 22050;              // F03: what the page's converter writes
constexpr uint32_t kIdleMs = 15000;            // no data this long: the phone is gone

SemaphoreHandle_t mutex = nullptr;
const void *owner = nullptr;  // the request the open upload belongs to
bool busy = false;   // an upload is open
bool failed = false; // the open upload was refused; ignore the rest of its data
Result last{200, ""};
File out;
char id[25] = "";
uint32_t lastDataMs = 0;
size_t written = 0;

struct Lock {
  bool held;
  explicit Lock(TickType_t wait = portMAX_DELAY) {
    if (!mutex) mutex = xSemaphoreCreateMutex();
    held = xSemaphoreTake(mutex, wait) == pdTRUE;
  }
  ~Lock() {
    if (held) xSemaphoreGive(mutex);
  }
};

void tmpPath(char *p, size_t cap) { snprintf(p, cap, "/sounds/%s.tmp", id); }
void wavPath(char *p, size_t cap) { snprintf(p, cap, "/sounds/%s.wav", id); }

// A sound from the card is playing: the card is busy (F12).
bool cardPlaying() {
  player::Stats p = player::stats();
  if (!p.playing || !p.current[0]) return false;
  library::Sound s;
  return library::find(p.current, s) && s.card;
}

Result fail(int code, const char *msg) {
  if (out) out.close();
  if (busy && id[0]) {
    char p[40];
    tmpPath(p, sizeof(p));
    if (fs::FS *card = sdcard::fs()) card->remove(p);
  }
  if (busy) Serial.printf("upload %s refused (%d): %s\n", id, code, msg);
  failed = true;
  last = {code, msg};
  return last;
}

void reset() {
  owner = nullptr;
  busy = false;
  failed = false;
  id[0] = 0;
  written = 0;
}

}  // namespace

bool active() {
  Lock l;
  return busy;
}

Result start(const void *who, const char *filename, size_t contentLength) {
  Lock l;
  if (busy && millis() - lastDataMs < kIdleMs) return {409, "another upload is running"};
  if (busy) fail(400, "dropped");  // a stale one: clean it up first
  reset();
  busy = true;
  owner = who;
  last = {200, ""};
  lastDataMs = millis();
  fs::FS *card = sdcard::fs();
  if (!card) return fail(503, "No SD card: recordings unavailable");
  // "<id>.wav", id a-z0-9_ (the page names the file this way)
  String name = filename ? filename : "";
  if (!name.endsWith(".wav")) return fail(400, "file must be name.wav with a-z, 0-9 and _");
  name.remove(name.length() - 4);
  if (!library::validId(name.c_str())) return fail(400, "file must be name.wav with a-z, 0-9 and _");
  strlcpy(id, name.c_str(), sizeof(id));
  library::Sound s;
  bool exists = library::find(id, s);
  if (exists && !(s.card && !s.pack[0])) return fail(409, "name already used by a built-in or pack call");
  if (!exists && library::count() >= library::kMaxSounds) return fail(413, "the caller holds 64 sounds at most");
  if (cardPlaying()) return fail(409, "stop playback first");
  // The multipart body is a little bigger than the file: a fair upper bound for the check.
  uint64_t room = sdcard::freeBytes();
  if (contentLength + kReserveBytes > room) return fail(413, "not enough room on the SD card");
  char p[40];
  tmpPath(p, sizeof(p));
  out = card->open(p, FILE_WRITE);
  if (!out) return fail(500, "can't write to the SD card");
  Serial.printf("upload %s: %u bytes coming heap=%u low=%u\n", id, (unsigned)contentLength, (unsigned)ESP.getFreeHeap(),
                (unsigned)heapmon::lowSince());
  return last;
}

Result data(const void *who, const uint8_t *buf, size_t len) {
  Lock l;
  if (!busy || who != owner) return {409, "another upload is running"};
  if (failed) return last;
  lastDataMs = millis();
  if (len && out.write(buf, len) != len) {
    sdcard::markGone();
    return fail(500, "SD card write failed");
  }
  written += len;
  return last;
}

Result finish(const void *who) {
  Lock l;
  if (!busy || who != owner) return {409, "another upload is running"};
  if (failed) {
    Result r = last;
    reset();
    return r;
  }
  out.close();
  fs::FS *card = sdcard::fs();
  char tmp[40], wav[40];
  tmpPath(tmp, sizeof(tmp));
  wavPath(wav, sizeof(wav));
  // Check the finished file: mono IMA ADPCM at 22.05 kHz, 60 s at most (F03).
  Result r{200, ""};
  File f = card ? card->open(tmp, "r") : File();
  if (!f) {
    r = {500, "SD card read failed"};
  } else {
    wav::Info info;
    const char *why = "";
    if (!wav::read(f, info, &why)) r = {400, why};
    else if (!info.adpcm) r = {400, "upload must be IMA ADPCM WAV (16-bit PCM is too big)"};
    else if (info.rate != kRate) r = {400, "upload must be 22.05 kHz"};
    else if (info.ms() > kMaxMs + 500) r = {400, "upload is longer than 60 s"};
    else if ((uint64_t)info.dataStart + info.dataBytes > f.size()) r = {400, "file is cut short"};
    f.close();
  }
  if (r.code != 200) {
    Result res = fail(r.code, r.msg);
    reset();
    return res;
  }
  // Replace: the old file goes only now that the new one is complete (FAT can't rename onto it).
  if (card->exists(wav)) card->remove(wav);
  if (!card->rename(tmp, wav)) {
    Result res = fail(500, "SD card rename failed");
    reset();
    return res;
  }
  Serial.printf("upload %s: %u bytes saved heap=%u low=%u\n", id, (unsigned)written, (unsigned)ESP.getFreeHeap(),
                (unsigned)heapmon::lowSince());
  char saved[sizeof(id)];
  strlcpy(saved, id, sizeof(saved));
  reset();
  uint32_t t0 = millis();
  sdcard::refresh();
  library::putUpload(saved);  // listed straight away, without rescanning the card
  Serial.printf("upload %s: listed in %u ms\n", saved, (unsigned)(millis() - t0));
  return r;
}

void loop() {
  Lock l(0);  // never wait on an upload in progress
  if (!l.held || !busy) return;
  if (millis() - lastDataMs < kIdleMs) return;
  Serial.printf("upload %s: no data for %u s, dropped\n", id, (unsigned)(kIdleMs / 1000));
  fail(400, "upload stopped");
  reset();
}

Result remove(const char *sid) {
  Lock l;
  library::Sound s;
  if (!sid || !library::find(sid, s)) return {404, "no such sound"};
  if (!s.card || s.pack[0]) return {400, "built-in and pack calls can't be deleted"};
  if (cardPlaying()) return {409, "stop playback first"};
  fs::FS *card = sdcard::fs();
  if (!card) return {503, "No SD card: recordings unavailable"};
  char p[40];
  snprintf(p, sizeof(p), "/sounds/%s.wav", sid);
  if (!card->remove(p)) return {500, "SD card delete failed"};
  uint32_t t0 = millis();
  sdcard::refresh();
  library::dropUpload(sid);
  Serial.printf("deleted %s: list updated in %u ms\n", sid, (unsigned)(millis() - t0));
  return {200, ""};
}

}  // namespace uploads
