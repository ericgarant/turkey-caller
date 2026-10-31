#include "ui.h"

#if TC_HAS_UI

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <esp_timer.h>

#include "bt.h"
#include "control.h"
#include "heapmon.h"
#include "library.h"
#include "player.h"
#include "version.h"

namespace ui {
namespace {

constexpr int kSda = 21, kScl = 22;
constexpr uint32_t kI2cHz = 100000;  // the PCF8574 backpack's rated speed
constexpr uint8_t kLcdAddrs[] = {0x27, 0x3F};  // PCF8574T, PCF8574AT (Eric's module: 0x3F)

constexpr uint32_t kPollUs = 5000;     // button sampling period
constexpr uint8_t kDebounceSamples = 6;  // 30 ms
constexpr uint32_t kHoldMs = 1000;
constexpr uint32_t kBacklightMs = 30000;
constexpr uint32_t kBootMs = 2000;     // "Turkey caller" and the version
constexpr uint32_t kNoteMs = 1500;     // "Volume 60", "Repeat on"
constexpr uint32_t kErrorMs = 3000;    // "No speaker", "Busy, try again"
constexpr uint32_t kWakeGiveUpMs = 12000;  // bt gives up after 8 s and sets error(); this is a backstop
constexpr uint32_t kScrollStepMs = 400, kScrollPauseMs = 1500;
constexpr uint32_t kTickMs = 100;      // screen refresh when nothing happens
constexpr int kVolumeStep = 5;

enum Button : uint8_t { kUp, kDown, kGreen, kRed, kButtons };
constexpr uint8_t kPins[kButtons] = {32, 33, 25, 26};
const char *const kButtonNames[kButtons] = {"yellow (up)", "blue (down)", "green (play)", "red (stop)"};

enum Kind : uint8_t { kPress, kHeld, kRelease, kReleaseAfterHold };
struct Event {
  uint8_t button, kind;
};
QueueHandle_t events;

// ---- Buttons: sampled by a 5 ms timer, so a slow LCD write never delays or loses a press ----

struct Debounce {
  bool down;
  bool held;
  uint8_t count;    // samples in a row that differ from `down`
  uint32_t downAt;
};
Debounce deb[kButtons];

void post(uint8_t b, uint8_t kind) {
  Event e{b, kind};
  xQueueSend(events, &e, 0);  // a full queue drops the event rather than block the timer
}

void sample(void *) {
  uint32_t now = millis();
  for (uint8_t b = 0; b < kButtons; b++) {
    Debounce &d = deb[b];
    bool raw = digitalRead(kPins[b]) == LOW;
    if (raw == d.down) {
      d.count = 0;
      if (d.down && !d.held && now - d.downAt >= kHoldMs) {
        d.held = true;
        post(b, kHeld);
      }
      continue;
    }
    if (++d.count < kDebounceSamples) continue;
    d.count = 0;
    d.down = raw;
    if (raw) {
      d.downAt = now;
      d.held = false;
      post(b, kPress);
    } else {
      post(b, d.held ? kReleaseAfterHold : kRelease);
    }
  }
}

// ---- LCD: only the characters that changed are written (a full rewrite is ~60 ms at 100 kHz) ----

LiquidCrystal_I2C *lcd = nullptr;
char onLcd[2][17];
bool lit = true;

void draw(const char *line1, const char *line2) {
  if (!lcd) return;
  const char *lines[2] = {line1, line2};
  for (uint8_t r = 0; r < 2; r++) {
    char want[17];
    snprintf(want, sizeof(want), "%-16.16s", lines[r]);
    for (uint8_t c = 0; c < 16;) {
      if (want[c] == onLcd[r][c]) {
        c++;
        continue;
      }
      uint8_t end = c;
      while (end < 16 && want[end] != onLcd[r][end]) end++;
      lcd->setCursor(c, r);
      for (uint8_t i = c; i < end; i++) lcd->write((uint8_t)want[i]);
      memcpy(&onLcd[r][c], &want[c], end - c);
      c = end;
    }
  }
}

void setBacklight(bool on) {
  if (!lcd || on == lit) return;
  lit = on;
  if (on) lcd->backlight();
  else lcd->noBacklight();
}

// The LCD's character set is ASCII: accented letters (UTF-8, Latin-1 range) lose their accent.
void toLcd(const char *in, char *out, size_t cap) {
  static const char kLatin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
  size_t n = 0;
  for (const uint8_t *p = (const uint8_t *)in; *p && n + 1 < cap; p++) {
    if (*p < 0x20) out[n++] = ' ';
    else if (*p < 0x80) out[n++] = (char)*p;
    else if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) out[n++] = kLatin1[*++p - 0x80];
    else if ((*p & 0xC0) == 0xC0) out[n++] = '?';  // other multi-byte characters
    // continuation bytes of those are dropped
  }
  out[n] = 0;
}

// ---- The call lists: the page's tabs, in the page's order ----

constexpr uint8_t kAnimals = 3;
const char *const kAnimalIds[kAnimals] = {"turkey", "moose", "deer"};
const char *const kAnimalNames[kAnimals] = {"Turkey", "Moose", "Deer"};

uint8_t animal = 0;
uint8_t list[library::kMaxSounds];  // library positions of this animal's calls
size_t listLen = 0;
uint32_t listRev = UINT32_MAX;
uint8_t listAnimal = UINT8_MAX;
size_t pos = 0;                     // selected call in `list`
char selected[sizeof(library::Sound::id)] = "";

size_t countFor(uint8_t a, uint8_t *out) {
  size_t n = 0;
  library::Sound s;
  for (size_t i = 0; library::get(i, s) && n < library::kMaxSounds; i++) {
    if (strcmp(s.animal, kAnimalIds[a]) == 0) {
      if (out) out[n] = (uint8_t)i;
      n++;
    }
  }
  return n;
}

// Rebuilds the list after an animal switch or a library change, keeping the selected call.
void refreshList() {
  if (listRev == library::rev() && listAnimal == animal) return;
  listRev = library::rev();
  listAnimal = animal;
  listLen = countFor(animal, list);
  library::Sound s;
  for (size_t i = 0; i < listLen; i++) {
    if (library::get(list[i], s) && strcmp(s.id, selected) == 0) {
      pos = i;
      return;
    }
  }
  if (pos >= listLen) pos = 0;
  if (listLen && library::get(list[pos], s)) strlcpy(selected, s.id, sizeof(selected));
  else selected[0] = 0;
}

void choose(size_t i) {
  library::Sound s;
  if (i >= listLen || !library::get(list[i], s)) return;
  pos = i;
  strlcpy(selected, s.id, sizeof(selected));
}

void nextAnimal() {
  for (uint8_t k = 1; k <= kAnimals; k++) {
    uint8_t a = (animal + k) % kAnimals;
    if (countFor(a, nullptr)) {
      animal = a;
      selected[0] = 0;
      pos = 0;
      refreshList();
      return;
    }
  }
}

// A call started elsewhere (a phone): show it, switching animal if needed.
void follow(const char *id) {
  library::Sound s;
  if (!library::find(id, s)) return;
  for (uint8_t a = 0; a < kAnimals; a++) {
    if (strcmp(s.animal, kAnimalIds[a]) == 0) animal = a;
  }
  strlcpy(selected, id, sizeof(selected));
  refreshList();
}

// ---- State ----

uint32_t bootUntil = 0;
char note[17] = "";             // shown on line 1 instead of the usual text, until noteUntil
uint32_t noteUntil = 0;
uint32_t lastInput = 0;
bool swallow[kButtons] = {};    // this push only woke the backlight
bool waking = false;            // a play from the box is waiting for the speaker
uint32_t wakingSince = 0;
uint32_t checkErrorAt = 0;      // after a play from the box: did the player refuse it?
char lastCurrent[32] = "";

void showNote(const char *text, uint32_t ms) {
  strlcpy(note, text, sizeof(note));
  noteUntil = millis() + ms;
}

void play() {
  if (!selected[0]) return;
  switch (control::play(selected, control::repeat())) {
    case bt::kPlaying:
      Serial.printf("box: play %s\n", selected);
      checkErrorAt = millis() + 1000;
      break;
    case bt::kWaking:
      Serial.printf("box: play %s, waking the speaker\n", selected);
      waking = true;
      wakingSince = millis();
      break;
    case bt::kNoSpeaker:
      showNote("No speaker", kErrorMs);
      break;
    case bt::kSearching:
      showNote("Busy, try again", kErrorMs);
      break;
  }
}

void handle(const Event &e, bool playing) {
  uint32_t now = millis();
  lastInput = now;
  if (e.kind == kPress && lcd && !lit) {
    setBacklight(true);
    swallow[e.button] = true;
    return;
  }
  if (swallow[e.button]) {
    if (e.kind == kRelease || e.kind == kReleaseAfterHold) swallow[e.button] = false;
    return;
  }
  bool busy = playing || waking;
  switch (e.button) {
    case kUp:
    case kDown:
      if (e.kind != kPress) return;
      if (busy) {
        int v = player::stats().volume + (e.button == kUp ? kVolumeStep : -kVolumeStep);
        control::setVolume(v);
        char t[17];
        snprintf(t, sizeof(t), "Volume %d", (int)constrain(v, 0, 100));
        showNote(t, kNoteMs);
      } else if (listLen) {
        choose(e.button == kUp ? (pos + listLen - 1) % listLen : (pos + 1) % listLen);
      }
      return;
    case kGreen:
      if (busy && e.kind == kHeld) {
        bool on = !control::repeat();
        control::setRepeat(on);
        showNote(on ? "Repeat on" : "Repeat off", kNoteMs);
      } else if (!busy && e.kind == kRelease) {
        play();
      }
      return;
    case kRed:
      if (e.kind != kPress) return;
      if (busy) {
        control::stop();
        waking = false;
      } else {
        nextAnimal();
      }
      return;
  }
}

// Line 2: the call's name, scrolled when it's over 16 characters.
void nameLine(const char *name, char *out) {
  static char last[40] = "";
  static size_t offset = 0;
  static uint32_t nextStep = 0;
  char ascii[40];
  toLcd(name, ascii, sizeof(ascii));
  uint32_t now = millis();
  if (strcmp(ascii, last) != 0) {
    strlcpy(last, ascii, sizeof(last));
    offset = 0;
    nextStep = now + kScrollPauseMs;
  }
  size_t len = strlen(ascii);
  if (len > 16 && (int32_t)(now - nextStep) >= 0) {
    if (offset >= len - 16) {
      offset = 0;
      nextStep = now + kScrollPauseMs;
    } else {
      offset++;
      nextStep = now + (offset == len - 16 ? kScrollPauseMs : kScrollStepMs);
    }
  }
  strlcpy(out, ascii + (len > 16 ? offset : 0), 17);
}

void render(const player::Stats &p) {
  uint32_t now = millis();
  char l1[17], l2[17];
  if ((int32_t)(bootUntil - now) > 0) {
    draw("Turkey caller", FW_VERSION);
    return;
  }
  library::Sound s;
  bool busy = p.playing || waking;
  if (busy) {
    if (p.playing) {
      uint32_t sec = p.posMs / 1000;
      snprintf(l1, sizeof(l1), "Playing %lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    } else {
      strlcpy(l1, "Speaker...", sizeof(l1));
    }
    size_t n = strlen(l1);
    while (n < 15) l1[n++] = ' ';
    l1[15] = control::repeat() ? 'R' : ' ';
    l1[16] = 0;
    const char *id = p.playing ? p.current : selected;
    nameLine(library::find(id, s) ? s.name : id, l2);
  } else if (!listLen) {
    strlcpy(l1, "No calls", sizeof(l1));
    l2[0] = 0;
  } else {
    char where[8];
    snprintf(where, sizeof(where), "%u/%u", (unsigned)(pos + 1), (unsigned)listLen);
    snprintf(l1, sizeof(l1), "%-*s%s", (int)(16 - strlen(where)), kAnimalNames[animal], where);
    nameLine(library::get(list[pos], s) ? s.name : "", l2);
  }
  if ((int32_t)(noteUntil - now) > 0) strlcpy(l1, note, sizeof(l1));
  draw(l1, l2);
}

void task(void *) {
  for (;;) {
    Event e;
    bool got = xQueueReceive(events, &e, pdMS_TO_TICKS(kTickMs)) == pdTRUE;
    player::Stats p = player::stats();
    refreshList();
    if (!listLen) nextAnimal();  // the turkey list is empty (no flash calls): show what there is
    // A call that started without the box (a phone): follow it.
    if (p.playing && strcmp(p.current, lastCurrent) != 0) {
      if (strcmp(p.current, selected) != 0) follow(p.current);
    }
    strlcpy(lastCurrent, p.playing ? p.current : "", sizeof(lastCurrent));
    if (got) {
      handle(e, p.playing);
      while (xQueueReceive(events, &e, 0) == pdTRUE) handle(e, p.playing);
      p = player::stats();
    }
    uint32_t now = millis();
    if (waking) {
      if (p.playing) {
        waking = false;
      } else if (bt::error().length() || now - wakingSince > kWakeGiveUpMs) {
        waking = false;
        showNote("No speaker", kErrorMs);
      }
    }
    if (checkErrorAt && (int32_t)(now - checkErrorAt) >= 0) {
      checkErrorAt = 0;
      String err = player::error();
      if (!p.playing && err.length()) showNote(err.c_str(), kErrorMs);
    }
    if (lit && lcd && now - lastInput > kBacklightMs) setBacklight(false);
    render(p);
    static bool stackLogged = false;
    if (!stackLogged && now > 60000) {
      stackLogged = true;
      Serial.printf("ui: stack low-water %u bytes free\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    }
  }
}

}  // namespace

void begin() {
  for (uint8_t b = 0; b < kButtons; b++) {
    pinMode(kPins[b], INPUT_PULLUP);
  }
  delay(2);
  for (uint8_t b = 0; b < kButtons; b++) {
    // A button that reads pressed now is wired across its always-connected legs (F15): it
    // stays "down" and never acts until it is released.
    deb[b].down = digitalRead(kPins[b]) == LOW;
    deb[b].held = true;
    if (deb[b].down) Serial.printf("ui: the %s button reads pressed at boot: check its wiring\n", kButtonNames[b]);
  }

  Wire.begin(kSda, kScl, kI2cHz);
  uint8_t addr = 0;
  for (uint8_t a : kLcdAddrs) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      addr = a;
      break;
    }
  }
  if (addr) {
    lcd = new LiquidCrystal_I2C(addr, 16, 2);
    lcd->init();
    lcd->backlight();
    memset(onLcd, ' ', sizeof(onLcd));
    onLcd[0][16] = onLcd[1][16] = 0;
    Serial.printf("LCD: 0x%02X\n", addr);
  } else {
    Serial.println("LCD: none (buttons still work)");
  }

  uint32_t now = millis();
  bootUntil = now + kBootMs;
  lastInput = now;
  events = xQueueCreate(16, sizeof(Event));
  // Below the player's decode task (priority 5 on core 1), so the box can't cause a dropout.
  xTaskCreatePinnedToCore(task, "ui", 3072, nullptr, 2, nullptr, 1);
  esp_timer_create_args_t args = {};
  args.callback = sample;
  args.name = "buttons";
  esp_timer_handle_t timer;
  esp_timer_create(&args, &timer);
  esp_timer_start_periodic(timer, kPollUs);
  Serial.printf("ui ready heap=%u low=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)heapmon::lowSince());
}

}  // namespace ui

#else

namespace ui {
void begin() {}
}  // namespace ui

#endif
