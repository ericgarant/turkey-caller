#include "sdcard.h"

#if TC_HAS_SD
#include <SD_MMC.h>
#endif

namespace sdcard {
namespace {
volatile bool mounted = false;
volatile bool gone = false;
uint32_t lastRemountMs = 0;
uint64_t freeB = 0, totalB = 0;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

void store(uint64_t f, uint64_t t) {
  portENTER_CRITICAL(&lock);
  freeB = f;
  totalB = t;
  portEXIT_CRITICAL(&lock);
}
}  // namespace

#if TC_HAS_SD
namespace {
bool mountOnce() {
  // 1-bit mode on CLK 14, CMD 15, D0 2: avoids GPIO 4 and 12 (12 is a boot strapping pin).
  // 20 MHz: the card only feeds ~11 KB/s of audio, and a slower clock is kinder to long wires.
  SD_MMC.setPins(14, 15, 2);
  if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 4)) return false;
  if (SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    return false;
  }
  return true;
}
}  // namespace

void begin() {
  // A card can miss its first init right after power-up (a battery pack's voltage still rising,
  // or a card left mid-command by a reset that didn't power it off): try a few times.
  bool ok = false;
  for (int i = 0; i < 3 && !ok; i++) {
    if (i) delay(250);
    ok = mountOnce();
  }
  if (!ok) {
    Serial.println("SD card: none (recordings unavailable)");
    return;
  }
  mounted = true;
  if (!SD_MMC.exists("/sounds")) SD_MMC.mkdir("/sounds");
  // An upload that never finished leaves "<id>.tmp" behind (F03): drop it.
  File dir = SD_MMC.open("/sounds");
  if (dir && dir.isDirectory()) {
    char stale[8][40];
    size_t n = 0;
    for (File f = dir.openNextFile(); f && n < 8; f = dir.openNextFile()) {
      String name = f.name();
      f.close();
      if (name.endsWith(".tmp")) snprintf(stale[n++], sizeof(stale[0]), "/sounds/%s", name.c_str());
    }
    dir.close();
    for (size_t i = 0; i < n; i++) {
      SD_MMC.remove(stale[i]);
      Serial.printf("SD card: removed unfinished upload %s\n", stale[i]);
    }
  }
  refresh();
  Serial.printf("SD card: %u MB, %u MB free\n", (unsigned)(totalB >> 20), (unsigned)(freeB >> 20));
}

fs::FS *fs() { return ready() ? &SD_MMC : nullptr; }

bool remount() {
  if (!mounted) return false;  // no card at boot: still no hot-plug
  uint32_t now = millis();
  if (lastRemountMs && now - lastRemountMs < kRemountGapMs) return false;
  lastRemountMs = now ? now : 1;
  gone = true;  // nothing may use the card while it is unmounted
  SD_MMC.end();
  bool ok = mountOnce();
  if (!ok) {
    delay(100);
    ok = mountOnce();
  }
  if (!ok) {
    store(0, 0);
    Serial.println("SD card: remount failed (card out or not answering)");
    return false;
  }
  gone = false;
  refresh();
  Serial.printf("SD card: remounted after a read error, %u MB free\n", (unsigned)(freeB >> 20));
  return true;
}

void refresh() {
  if (!ready()) return;
  uint64_t t = SD_MMC.totalBytes(), u = SD_MMC.usedBytes();
  store(t > u ? t - u : 0, t);
}
#else
void begin() {}
fs::FS *fs() { return nullptr; }
bool remount() { return false; }
void refresh() {}
#endif

bool ready() { return mounted && !gone; }

void markGone() {
  if (!mounted || gone) return;
  gone = true;
  store(0, 0);
  Serial.println("SD card: read failed, treating the card as removed until it remounts");
}

uint64_t freeBytes() {
  portENTER_CRITICAL(&lock);
  uint64_t f = freeB;
  portEXIT_CRITICAL(&lock);
  return f;
}

uint64_t totalBytes() {
  portENTER_CRITICAL(&lock);
  uint64_t t = totalB;
  portEXIT_CRITICAL(&lock);
  return t;
}

}  // namespace sdcard
