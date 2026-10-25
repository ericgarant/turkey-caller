// sdcard: the microSD card on the WROVER-E (F12). Phone uploads live in /sounds/ on it; the
// built-in calls stay in LittleFS. A missing or pulled card never stops the built-ins.
#pragma once
#include <Arduino.h>
#include <FS.h>

namespace sdcard {

void begin();          // mount once at boot (no hot-plug); removes leftover *.tmp uploads
bool ready();          // mounted and not marked gone
fs::FS *fs();          // the card's file system, or nullptr when not ready
// A read failed on the card: treat it as removed until the next restart. Logs once.
void markGone();
uint64_t freeBytes();  // cached; 0 when there is no card
uint64_t totalBytes();
void refresh();        // re-read the free space (after a write)

}  // namespace sdcard
