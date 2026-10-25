#include "library.h"

#include <LittleFS.h>
#include <esp_heap_caps.h>

#include "sdcard.h"
#include "wav.h"

namespace library {
namespace {

constexpr size_t kMaxIndexBytes = 4096;  // each index.json is read whole; keep it small (F10)
constexpr size_t kMaxPacks = 8;
constexpr uint32_t kMaxPackClipMs = 60000;  // F14: pack clips up to 60 s each

Sound *sounds = nullptr;  // exactly `n` entries, in PSRAM when the board has it (F14)
size_t n = 0;
uint32_t freeBytes = 0, totalBytes = 0;
volatile uint32_t listRev = 0;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

// PSRAM when there is some (WROVER-E), else the normal heap (WROOM).
void *allocList(size_t bytes) {
  void *p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : calloc(1, bytes);
}

struct Entry {
  char id[25], name[33], group[6], animal[8], hint[48];
};

// Minimal reader for an index: a JSON array of flat objects with string values, e.g.
// [{"id":"tree_call","name":"Tree call","group":"soft","animal":"turkey","hint":"..."}, ...].
// Calls `fn` once per object. Unknown keys are ignored; long values are cut to fit.
template <typename Fn>
void parseIndex(const char *p, Fn fn) {
  char key[16], val[64];
  Entry e;
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
    e = Entry{};
    for (;;) {
      skipWs();
      if (*p == '}') { p++; break; }
      if (!readStr(key, sizeof(key))) return;
      skipWs();
      if (*p++ != ':') return;
      skipWs();
      if (!readStr(val, sizeof(val))) return;
      if (!strcmp(key, "id")) strlcpy(e.id, val, sizeof(e.id));
      else if (!strcmp(key, "name")) strlcpy(e.name, val, sizeof(e.name));
      else if (!strcmp(key, "group")) strlcpy(e.group, val, sizeof(e.group));
      else if (!strcmp(key, "animal")) strlcpy(e.animal, val, sizeof(e.animal));
      else if (!strcmp(key, "hint")) strlcpy(e.hint, val, sizeof(e.hint));
    }
    fn(e);
  }
}

// Reads a whole index file (at most kMaxIndexBytes) and parses it.
template <typename Fn>
void readIndex(fs::FS &fs, const char *file, Fn fn) {
  File idx = fs.open(file, "r");
  if (!idx) return;
  if (idx.size() >= kMaxIndexBytes) {
    Serial.printf("%s too big, ignored\n", file);
    idx.close();
    return;
  }
  char *text = (char *)malloc(idx.size() + 1);
  if (text) {
    size_t len = idx.read((uint8_t *)text, idx.size());
    text[len] = 0;
    idx.close();
    parseIndex(text, fn);
    free(text);
  } else {
    idx.close();
  }
}

// Reads a clip's header. Returns false (and logs) for a file that can't be played.
bool probe(fs::FS &fs, const char *file, Sound &s) {
  File f = fs.open(file, "r");
  if (!f) return false;
  wav::Info info;
  const char *why = "";
  bool ok = wav::read(f, info, &why);
  s.bytes = f.size();
  f.close();
  if (ok && (uint64_t)info.dataStart + info.dataBytes > s.bytes) {
    ok = false;  // the player would hit a short read, which on the card means "card gone"
    why = "audio data runs past the end of the file";
  }
  if (!ok) {
    Serial.printf("sound %s skipped: %s\n", file, why);
    return false;
  }
  s.ms = info.ms();
  return true;
}

// "tree_call" -> "Tree call"
void defaultName(const char *id, char *out, size_t cap) {
  strlcpy(out, id, cap);
  for (char *c = out; *c; c++)
    if (*c == '_') *c = ' ';
  if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
}

bool knownGroup(const char *g) {
  static const char *const kGroups[] = {"soft", "loud", "care", "pack", "moose", "deer", "mine"};
  for (const char *k : kGroups)
    if (!strcmp(g, k)) return true;
  return false;
}

bool knownAnimal(const char *a) { return !strcmp(a, "turkey") || !strcmp(a, "moose") || !strcmp(a, "deer"); }

// Pack folders: a-z, 0-9, _ and -, at most 15 characters.
bool validPack(const char *p) {
  size_t len = strlen(p);
  if (len == 0 || len >= sizeof(Sound::pack)) return false;
  for (const char *c = p; *c; c++)
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-')) return false;
  return true;
}

int find(const char *id) {
  for (size_t i = 0; i < n; i++)
    if (!strcmp(sounds[i].id, id)) return (int)i;
  return -1;
}

}  // namespace

bool validId(const char *id) {
  size_t len = id ? strlen(id) : 0;
  if (len == 0 || len > 24) return false;
  for (const char *c = id; *c; c++)
    if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) return false;
  return true;
}

void path(const Sound &s, char *out, size_t cap) {
  if (s.pack[0]) snprintf(out, cap, "/packs/%s/%s.wav", s.pack, s.id);
  else snprintf(out, cap, "/sounds/%s.wav", s.id);
}

void begin() {
  uint32_t startMs = millis();
  size_t count = 0;
  // Scratch list while scanning (~9 KB), freed once the list is built.
  Sound *found = (Sound *)allocList(kMaxSounds * sizeof(Sound));
  if (!found) {
    Serial.println("library: out of memory");
    return;
  }
  auto listed = [&](const char *id) {
    for (size_t i = 0; i < count; i++)
      if (!strcmp(found[i].id, id)) return true;
    return false;
  };
  // Adds one sound from an index entry (built-in or pack). `where` is for the log.
  auto addEntry = [&](fs::FS &fs, const Entry &e, const char *pack, const char *where) {
    if (count >= kMaxSounds || !validId(e.id)) return;
    if (listed(e.id)) {
      Serial.printf("%s: %s skipped, id already used\n", where, e.id);
      return;
    }
    Sound s{};
    strlcpy(s.id, e.id, sizeof(s.id));
    strlcpy(s.pack, pack, sizeof(s.pack));
    s.card = pack[0] != 0;
    char file[56];
    path(s, file, sizeof(file));
    if (!probe(fs, file, s)) return;  // missing or broken: not listed
    if (s.card && s.ms > kMaxPackClipMs) {
      Serial.printf("sound %s skipped: %u s long, packs take 60 s at most\n", file, (unsigned)(s.ms / 1000));
      return;
    }
    if (e.name[0]) strlcpy(s.name, e.name, sizeof(s.name));
    else defaultName(e.id, s.name, sizeof(s.name));
    strlcpy(s.animal, knownAnimal(e.animal) ? e.animal : "turkey", sizeof(s.animal));
    if (knownGroup(e.group)) strlcpy(s.group, e.group, sizeof(s.group));
    else if (s.card && strcmp(s.animal, "turkey")) strlcpy(s.group, s.animal, sizeof(s.group));  // e.g. "moose"
    strlcpy(s.hint, e.hint, sizeof(s.hint));
    found[count++] = s;
  };
  // Adds every .wav in /sounds/ not listed yet: flash leftovers, or the card's uploads.
  auto scan = [&](fs::FS &fs, bool card) {
    File dir = fs.open("/sounds");
    if (!dir || !dir.isDirectory()) return;
    for (File f = dir.openNextFile(); f && count < kMaxSounds; f = dir.openNextFile()) {
      String fname = f.name();
      f.close();
      if (!fname.endsWith(".wav")) continue;
      fname.remove(fname.length() - 4);
      if (!validId(fname.c_str())) {
        Serial.printf("sound %s.wav skipped: name must be a-z, 0-9 and _ (24 at most)\n", fname.c_str());
        continue;
      }
      if (listed(fname.c_str())) {
        if (card) Serial.printf("card sound %s.wav skipped: name already used by a built-in or pack call\n", fname.c_str());
        continue;
      }
      Sound s{};
      strlcpy(s.id, fname.c_str(), sizeof(s.id));
      s.card = card;
      char file[56];
      path(s, file, sizeof(file));
      if (!probe(fs, file, s)) continue;
      defaultName(s.id, s.name, sizeof(s.name));
      strlcpy(s.animal, "turkey", sizeof(s.animal));
      if (card) strlcpy(s.group, "mine", sizeof(s.group));
      found[count++] = s;
    }
    dir.close();
  };

  // 1. Built-ins: the flash index in its order, then other flash files.
  readIndex(LittleFS, "/sounds/index.json", [&](const Entry &e) { addEntry(LittleFS, e, "", "sounds/index.json"); });
  scan(LittleFS, false);

  if (fs::FS *card = sdcard::fs()) {
    // 2. Packs, alphabetical by folder.
    char packs[kMaxPacks][sizeof(Sound::pack)];
    size_t np = 0;
    File dir = card->exists("/packs") ? card->open("/packs") : File();  // no folder: no error line
    if (dir && dir.isDirectory()) {
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        bool isDir = f.isDirectory();
        String name = f.name();
        f.close();
        if (!isDir) continue;
        if (!validPack(name.c_str())) {
          Serial.printf("pack %s skipped: folder name must be a-z, 0-9, _ or - (15 at most)\n", name.c_str());
          continue;
        }
        if (np == kMaxPacks) {
          Serial.printf("pack %s skipped: more than %u packs\n", name.c_str(), (unsigned)kMaxPacks);
          continue;
        }
        strlcpy(packs[np++], name.c_str(), sizeof(packs[0]));
      }
    }
    if (dir) dir.close();
    qsort(packs, np, sizeof(packs[0]), [](const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); });
    for (size_t i = 0; i < np; i++) {
      char file[48];
      snprintf(file, sizeof(file), "/packs/%s/index.json", packs[i]);
      size_t before = count;
      readIndex(*card, file, [&](const Entry &e) { addEntry(*card, e, packs[i], packs[i]); });
      Serial.printf("pack %s: %u sounds\n", packs[i], (unsigned)(count - before));
    }
    // 3. Uploads.
    scan(*card, true);
  }

  uint32_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  Sound *list = count ? (Sound *)allocList(count * sizeof(Sound)) : nullptr;
  if (list) memcpy(list, found, count * sizeof(Sound));
  else count = 0;
  free(found);
  portENTER_CRITICAL(&lock);
  Sound *old = sounds;
  sounds = list;
  n = count;
  totalBytes = total;
  freeBytes = total > used ? total - used : 0;
  listRev = listRev + 1;
  portEXIT_CRITICAL(&lock);
  free(old);
  Serial.printf("library: %u sounds, %u of %u bytes free, scan %u ms\n", (unsigned)count, (unsigned)freeBytes, (unsigned)totalBytes,
                (unsigned)(millis() - startMs));
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

bool find(const char *id, Sound &out) {
  portENTER_CRITICAL(&lock);
  int i = find(id);
  if (i >= 0) out = sounds[i];
  portEXIT_CRITICAL(&lock);
  return i >= 0;
}

bool builtIn(const char *id) {
  Sound s;
  return find(id, s) && !s.card;
}

uint32_t rev() { return listRev; }
uint32_t fsFree() { return freeBytes; }
uint32_t fsTotal() { return totalBytes; }

}  // namespace library
