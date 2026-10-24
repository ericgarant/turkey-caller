// bt: Bluetooth A2DP source. Connects to the saved speaker, remembers it, reconnects.
#pragma once
#include <Arduino.h>

namespace bt {

struct Device {
  char name[33];
  char addr[18];
  int rssi;
};

enum PlayResult { kPlaying, kWaking, kNoSpeaker, kSearching };

void begin();                  // start the BT stack (call before Wi-Fi)
bool connected();
bool busy();                   // the speaker is connecting or starting its stream: heap is tight for a few seconds
const char *state();           // "idle" (not searching), "searching" (one ~25 s round per request), "connecting", "connected", "disconnecting"
void requestSearch();          // start looking for speakers (from any task)
void notePlay();               // a call is about to play: share the radio with the speaker now (any task)
// A tap (any task). Plays now if the stream is ready; otherwise wakes the speaker (stream start
// or reconnect) and plays once it answers, or gives up after 8 s with error() set (F07).
PlayResult requestPlay(const char *id, bool loop);
void cancelWake();             // Stop pressed while waking
const char *stream();          // "ready", "waking", "suspended" or "down" (F07)
String error();                // last wake failure for the page ("" = none), cleared by the next tap
void loop();                   // call from loop(): starts a requested search
String speakerName();          // saved speaker name ("" = none yet)
size_t devices(Device *out, size_t max);  // speakers seen while searching
void use(const char *name, const char *addr);  // save and connect by address, no restart (any task)
void forget();                                // disconnect and clear the saved speaker, no restart (any task)

}  // namespace bt
