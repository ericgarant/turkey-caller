// motion: the HC-SR501 PIR on GPIO 34 and the LED on GPIO 33 (F13 step 1, WROVER-E only).
// The LED is on while the sensor is high, plus 2 s, when "LED on motion" is on.
#pragma once
#include <Arduino.h>

namespace motion {

void begin();         // after settings::begin()
void loop();          // from loop(): a pin read, no allocation
bool active();        // the sensor reports motion now (false during the warm-up)
uint32_t lastAt();    // seconds since boot of the last trigger, 0 if none

}  // namespace motion
