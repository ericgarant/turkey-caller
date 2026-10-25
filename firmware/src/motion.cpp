#include "motion.h"

#include <esp_system.h>

#include "settings.h"

namespace motion {
namespace {

#if TC_HAS_PIR
// GPIO 34: input-only, with no internal pulls, and not on the SD slot (13 was its DAT3). Eric
// moved the PIR there on 25 Oct after it came off GPIO 13.
constexpr int kPirPin = 34;
constexpr int kLedPin = 33;
#endif
constexpr uint32_t kHoldMs = 2000;     // LED stays on this long after the sensor drops
constexpr uint32_t kWarmUpMs = 60000;  // the HC-SR501 false-triggers for 30 to 60 s after power-on

bool ready = false;          // warm-up over
volatile bool high = false;  // sensor output, debounced by the module itself
volatile uint32_t lastAtS = 0;
uint32_t lowSinceMs = 0;
bool ledOn = false;

void setLed(bool on) {
#if TC_HAS_PIR
  if (on != ledOn) digitalWrite(kLedPin, on ? HIGH : LOW);
#endif
  ledOn = on;
}

}  // namespace

void begin() {
#if TC_HAS_PIR
  pinMode(kPirPin, INPUT);
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, LOW);
  // A restart (settings, BOOT reset, crash) doesn't cut the sensor's power: no warm-up then.
  ready = esp_reset_reason() != ESP_RST_POWERON;
  Serial.printf("motion: PIR on GPIO %d, LED on GPIO %d, %s\n", kPirPin, kLedPin,
                ready ? "ready" : "warming up for 60 s after power-on");
#endif
}

void loop() {
#if TC_HAS_PIR
  uint32_t now = millis();
  if (!ready) {
    if (now < kWarmUpMs) return;
    ready = true;
    Serial.println("motion: sensor ready");
  }
  bool h = digitalRead(kPirPin) == HIGH;
  if (h && !high) {
    lastAtS = now / 1000;
    if (lastAtS == 0) lastAtS = 1;  // 0 means "never"
    Serial.printf("motion at %lus\n", (unsigned long)(now / 1000));
  }
  if (!h && high) lowSinceMs = now;
  high = h;
  setLed(settings::ledMotion() && (h || (lastAtS && now - lowSinceMs < kHoldMs)));
#endif
}

bool active() { return high; }
uint32_t lastAt() { return lastAtS; }

}  // namespace motion
