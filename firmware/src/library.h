// library: the sound list. Order, names and groups come from /sounds/index.json (hand-written,
// in git); length comes from each WAV header. Files missing from the index are listed last.
#pragma once
#include <Arduino.h>

namespace library {

struct Sound {
  char id[25];     // file name without .wav: a-z0-9_, at most 24 characters
  char name[33];   // display name
  char group[6];   // soft, loud, care, pack (Dindon pack) or "" (shown under "Other sounds")
  uint32_t bytes;  // file size
  uint32_t ms;     // length from the WAV header
};

void begin();                          // read the index and every clip header (after LittleFS mounts)
size_t count();
bool get(size_t i, Sound &out);        // i-th sound in display order
bool has(const char *id);              // a playable sound with this id exists
uint32_t fsFree();                     // bytes free on LittleFS (cached, updated by begin())
uint32_t fsTotal();
bool validId(const char *id);

}  // namespace library
