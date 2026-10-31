#include "control.h"

#include "heapmon.h"
#include "player.h"

namespace control {
namespace {
volatile bool repeatOn = false;
}

bt::PlayResult play(const char *id, bool loop) {
  bt::PlayResult r = bt::requestPlay(id, loop);
  if (r == bt::kPlaying || r == bt::kWaking) repeatOn = loop;
  return r;
}

void stop() {
  Serial.printf("stop heap=%u low=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)heapmon::lowSince());
  bt::cancelWake();
  player::stop();
}

bool repeat() { return repeatOn; }

void setRepeat(bool on) {
  repeatOn = on;
  player::setLoop(on);
  Serial.printf("repeat %s heap=%u low=%u\n", on ? "on" : "off", (unsigned)ESP.getFreeHeap(), (unsigned)heapmon::lowSince());
}

void setVolume(long level) { player::setVolume((uint8_t)constrain(level, 0, 100)); }

}  // namespace control
