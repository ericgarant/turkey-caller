// control: the actions the page and the box's buttons share (F15), so both drive the same
// player, volume and Repeat switch. Safe to call from any task.
#pragma once
#include <Arduino.h>

#include "bt.h"

namespace control {

// A tap: plays now, or wakes the speaker first (F07). A play also sets the shared Repeat switch
// to `loop`, except when it is refused (no speaker, searching).
bt::PlayResult play(const char *id, bool loop);
void stop();                // also cancels a speaker wake
bool repeat();              // the Repeat switch, shared by every page and the box (off after a restart)
void setRepeat(bool on);    // also changes Repeat for the call playing now
void setVolume(long level); // clamped to 0-100

}  // namespace control
