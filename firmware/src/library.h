// library: the sound list, merged in this order (F12, F14):
//   1. built-in calls in LittleFS: order, names, groups and hints from /sounds/index.json (in
//      git), then any other flash .wav under "Other sounds";
//   2. call packs on the SD card, alphabetical by folder: /packs/<pack>/index.json lists the
//      pack's clips (only those; a WAV without an entry is ignored);
//   3. recordings on the card in /sounds/ (uploads), in group "mine" ("Your recordings").
// Lengths come from each WAV header. Ids share one namespace: a later sound with an id already
// listed is skipped and logged, so the first one wins.
#pragma once
#include <Arduino.h>

namespace library {

constexpr size_t kMaxSounds = 64;  // built-ins, packs and uploads together (F14)

struct Sound {
  char id[25];     // file name without .wav: a-z0-9_, at most 24 characters
  char name[33];   // display name
  char group[6];   // soft, loud, care, pack (Dindon pack), moose, deer, mine (uploads) or "" ("Other sounds")
  char animal[8];  // turkey, moose or deer
  char hint[48];   // short note shown on the button ("" = the page's own)
  char pack[16];   // pack folder on the card ("" = flash, or /sounds/ on the card)
  uint32_t bytes;  // file size
  uint32_t ms;     // length from the WAV header
  bool card;       // on the SD card, not in flash
};

void begin();                          // read the indexes and every clip header (after LittleFS and the card mount)
size_t count();
bool get(size_t i, Sound &out);        // i-th sound in display order
bool has(const char *id);              // a playable sound with this id exists
bool find(const char *id, Sound &out); // the sound with this id
bool builtIn(const char *id);          // a built-in call (in flash) has this id
// The clip's file: "/sounds/<id>.wav" (flash or card) or "/packs/<pack>/<id>.wav" (card).
void path(const Sound &s, char *out, size_t cap);
uint32_t rev();                        // bumped by every begin(): phones reload the list when it moves
uint32_t fsFree();                     // bytes free on LittleFS (cached, updated by begin())
uint32_t fsTotal();
bool validId(const char *id);
// Uploads (F03): list or relist one recording in /sounds/ on the card, or drop one, without
// rescanning every file (a full begin() takes over 1 s with 40-odd clips on the card).
bool putUpload(const char *id);
void dropUpload(const char *id);

}  // namespace library
