// library: the sound list. Built-in calls live in LittleFS: order, names and groups come from
// /sounds/index.json (hand-written, in git), length from each WAV header, and files missing from
// the index are listed after it. Recordings on the SD card (/sounds/ there, F12) come last, in
// group "mine" ("Your recordings"). Ids share one namespace: a card file with a built-in's id is
// skipped.
#pragma once
#include <Arduino.h>

namespace library {

struct Sound {
  char id[25];     // file name without .wav: a-z0-9_, at most 24 characters
  char name[33];   // display name
  char group[6];   // soft, loud, care, pack (Dindon pack), mine (on the SD card) or "" ("Other sounds")
  uint32_t bytes;  // file size
  uint32_t ms;     // length from the WAV header
  bool card;       // on the SD card, not in flash
};

void begin();                          // read the index and every clip header (after LittleFS and the card mount)
size_t count();
bool get(size_t i, Sound &out);        // i-th sound in display order
bool has(const char *id);              // a playable sound with this id exists
bool find(const char *id, Sound &out); // the sound with this id
bool builtIn(const char *id);          // a built-in call (in flash) has this id
uint32_t fsFree();                     // bytes free on LittleFS (cached, updated by begin())
uint32_t fsTotal();
bool validId(const char *id);

}  // namespace library
