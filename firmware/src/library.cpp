#include "library.h"

#include <LittleFS.h>

#include "wav.h"

namespace library {
namespace {

constexpr size_t kMaxSounds = 40;
constexpr size_t kMaxIndexBytes = 4096;

Sound *sounds = nullptr;  // exactly `n` entries on the heap: a fixed 40-entry table cost ~3 KB of RAM
size_t n = 0;
uint32_t freeBytes = 0, totalBytes = 0;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

// Minimal reader for the index: a JSON array of flat objects with string values, e.g.
// [{"id":"tree_call","name":"Tree call","group":"soft"}, ...]. Calls `fn` once per object.
template <typename Fn>
void parseIndex(const char *p, Fn fn) {
  char key[16], val[40], id[25], name[33], group[6];
  auto skipWs = [&]() { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',') p++; };
  auto readStr = [&](char *out, size_t cap) -> bool {
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
      char c = *p++;
      if (c == '\\' && *p) c = *p++;  // keep the escaped character as is
      if (i + 1 < cap) out[i++] = c;
    }
    out[i] = 0;
    if (*p == '"') p++;
    return true;
  };
  skipWs();
  if (*p++ != '[') return;
  for (;;) {
    skipWs();
    if (*p != '{') return;
    p++;
    id[0] = name[0] = group[0] = 0;
    for (;;) {
      skipWs();
      if (*p == '}') { p++; break; }
      if (!readStr(key, sizeof(key))) return;
      skipWs();
      if (*p++ != ':') return;
      skipWs();
      if (!readStr(val, sizeof(val))) return;
      if (!strcmp(key, "id")) strlcpy(id, val, sizeof(id));
      else if (!strcmp(key, "name")) strlcpy(name, val, sizeof(name));
      else if (!strcmp(key, "group")) strlcpy(group, val, sizeof(group));
    }
    fn(id, name, group);
  }
}

// Reads a clip's header. Returns false (and logs) for a file that can't be played.
bool probe(const char *id, Sound &s) {
  char path[40];
  snprintf(path, sizeof(path), "/sounds/%s.wav", id);
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  wav::Info info;
  const char *why = "";
  bool ok = wav::read(f, info, &why);
  s.bytes = f.size();
  f.close();
  if (!ok) {
    Serial.printf("sound %s skipped: %s\n", path, why);
    return false;
  }
  strlcpy(s.id, id, sizeof(s.id));
  s.ms = info.ms();
  return true;
}

int find(const char *id) {
  for (size_t i = 0; i < n; i++)
    if (!strcmp(sounds[i].id, id)) return (int)i;
  return -1;
}

// "tree_call" -> "Tree call"
void defaultName(const char *id, char *out, size_t cap) {
  strlcpy(out, id, cap);
  for (char *c = out; *c; c++)
    if (*c == '_') *c = ' ';
  if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
}

}  // namespace

bool validId(const char *id) {
  size_t len = id ? strlen(id) : 0;
  if (len == 0 || len > 24) return false;
  for (const char *c = id; *c; c++)
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) return false;
  return true;
}

void begin() {
  size_t count = 0;
  // Scratch list while scanning (~3 KB), freed before the radios start.
  Sound *found = (Sound *)calloc(kMaxSounds, sizeof(Sound));
  if (!found) {
    Serial.println("library: out of memory");
    return;
  }

  // 1. Sounds named in the index, in its order, when their file exists and plays.
  File idx = LittleFS.open("/sounds/index.json", "r");
  if (idx && idx.size() < kMaxIndexBytes) {
    char *text = (char *)malloc(idx.size() + 1);
    if (text) {
      size_t len = idx.read((uint8_t *)text, idx.size());
      text[len] = 0;
      parseIndex(text, [&](const char *id, const char *name, const char *group) {
        if (count >= kMaxSounds || !validId(id)) return;
        for (size_t i = 0; i < count; i++)
          if (!strcmp(found[i].id, id)) return;
        Sound s{};
        if (!probe(id, s)) return;  // missing or broken: not listed
        if (name[0]) strlcpy(s.name, name, sizeof(s.name));
        else defaultName(id, s.name, sizeof(s.name));
        if (!strcmp(group, "soft") || !strcmp(group, "loud") || !strcmp(group, "care")) strlcpy(s.group, group, sizeof(s.group));
        found[count++] = s;
      });
      free(text);
    }
  } else if (idx) {
    Serial.println("sounds/index.json too big, ignored");
  }
  if (idx) idx.close();

  // 2. Every other .wav file, in directory order.
  File dir = LittleFS.open("/sounds");
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f && count < kMaxSounds; f = dir.openNextFile()) {
      String fname = f.name();
      f.close();
      if (!fname.endsWith(".wav")) continue;
      fname.remove(fname.length() - 4);
      if (!validId(fname.c_str())) {
        Serial.printf("sound %s.wav skipped: name must be a-z, 0-9 and _ (24 at most)\n", fname.c_str());
        continue;
      }
      bool known = false;
      for (size_t i = 0; i < count && !known; i++) known = !strcmp(found[i].id, fname.c_str());
      if (known) continue;
      Sound s{};
      if (!probe(fname.c_str(), s)) continue;
      defaultName(s.id, s.name, sizeof(s.name));
      found[count++] = s;
    }
  }

  uint32_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  Sound *list = count ? (Sound *)malloc(count * sizeof(Sound)) : nullptr;
  if (list) memcpy(list, found, count * sizeof(Sound));
  else count = 0;
  free(found);
  portENTER_CRITICAL(&lock);
  Sound *old = sounds;
  sounds = list;
  n = count;
  totalBytes = total;
  freeBytes = total > used ? total - used : 0;
  portEXIT_CRITICAL(&lock);
  free(old);
  Serial.printf("library: %u sounds, %u of %u bytes free\n", (unsigned)count, (unsigned)freeBytes, (unsigned)totalBytes);
}

size_t count() {
  portENTER_CRITICAL(&lock);
  size_t c = n;
  portEXIT_CRITICAL(&lock);
  return c;
}

bool get(size_t i, Sound &out) {
  portENTER_CRITICAL(&lock);
  bool ok = i < n;
  if (ok) out = sounds[i];
  portEXIT_CRITICAL(&lock);
  return ok;
}

bool has(const char *id) {
  portENTER_CRITICAL(&lock);
  bool ok = find(id) >= 0;
  portEXIT_CRITICAL(&lock);
  return ok;
}

uint32_t fsFree() { return freeBytes; }
uint32_t fsTotal() { return totalBytes; }

}  // namespace library
