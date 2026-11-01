// sdcard: the microSD card on the WROVER-E (F12). Phone uploads live in /sounds/ on it; the
// built-in calls stay in LittleFS. A missing or pulled card never stops the built-ins.
#pragma once
#include <Arduino.h>
#include <FS.h>

namespace sdcard {

void begin();          // mount at boot (no hot-plug: a card added later needs a restart); removes leftover *.tmp uploads
bool ready();          // mounted and not marked gone
fs::FS *fs();          // the card's file system, or nullptr when not ready
// A read failed on the card: treat it as removed until remount() works or the next restart.
// Logs once.
void markGone();
// Unmount and mount the card again, after a read error or while it is marked gone. One
// glitch (a power dip on a battery pack, a card that stopped answering) used to need a restart.
// Only for a card that mounted at boot, at most once every kRemountGapMs, and never while an
// upload is writing (the caller checks). Blocks up to ~1 s. Returns true when the card is back.
constexpr uint32_t kRemountGapMs = 3000;
bool remount();
uint64_t freeBytes();  // cached; 0 when there is no card
uint64_t totalBytes();
void refresh();        // re-read the free space (after a write)

}  // namespace sdcard
