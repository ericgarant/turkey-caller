#pragma once
// Heap low-water marks. The heap's own minimum only ever goes down, so after the first dip it
// can't show what later events cost. This keeps the boot-wide minimum and, separately, the low
// since the previous lowSince() call, so each log line shows what happened since the one before.
#include <Arduino.h>

namespace heapmon {

void begin();          // call once at the end of setup()
uint32_t minEver();    // lowest free internal heap since boot
uint32_t lowSince();   // lowest since the previous lowSince() call (any task); starts a new window

}  // namespace heapmon
