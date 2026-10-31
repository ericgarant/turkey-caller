// F15 probe: LCD1602 (I2C) on SDA 21 / SCL 22 and four big buttons to GND.
// Prints the LCD's address, then each press and hold on serial and on the LCD.
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

struct Button {
  const char *name;
  uint8_t pin;
  bool down = false;       // debounced state
  bool raw = false;
  uint32_t changedAt = 0;  // last raw change
  uint32_t downAt = 0;
  uint32_t presses = 0;
};

static Button buttons[] = {{"Yellow", 32}, {"Blue", 33}, {"Green", 25}, {"Red", 26}};
static const uint32_t kDebounceMs = 30, kHoldMs = 1000, kBacklightMs = 30000;

static LiquidCrystal_I2C *lcd = nullptr;
static uint8_t lcdAddr = 0;
static bool backlight = true;
static uint32_t lastPress = 0;

static uint8_t findLcd() {
  uint8_t found = 0;
  Serial.print("I2C scan:");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      if (!found && ((a >= 0x20 && a <= 0x27) || (a >= 0x38 && a <= 0x3F))) found = a;  // PCF8574(A) backpacks
    }
  }
  Serial.println(found ? "" : " nothing");
  return found;
}

// Rewrites both lines (32 characters) and reports how long it took.
static void show(const char *line1, const char *line2) {
  if (!lcd) return;
  char a[17], b[17];
  snprintf(a, sizeof a, "%-16s", line1);
  snprintf(b, sizeof b, "%-16s", line2);
  uint32_t t = micros();
  lcd->setCursor(0, 0);
  lcd->print(a);
  lcd->setCursor(0, 1);
  lcd->print(b);
  Serial.printf("  (LCD rewrite %lu us)\n", (unsigned long)(micros() - t));
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nui-probe: LCD on SDA 21 / SCL 22, buttons Yellow 32, Blue 33, Green 25, Red 26");
  for (auto &b : buttons) pinMode(b.pin, INPUT_PULLUP);
  Wire.begin(21, 22, 100000);
  lcdAddr = findLcd();
  if (lcdAddr) {
    Serial.printf("LCD: 0x%02X\n", lcdAddr);
    lcd = new LiquidCrystal_I2C(lcdAddr, 16, 2);
    lcd->init();
    lcd->backlight();
    show("Turkey caller", "probe");
  } else {
    Serial.println("LCD: none (check SDA 21, SCL 22, VCC on 5V, GND). Buttons still report on serial.");
  }
  for (auto &b : buttons) {
    if (digitalRead(b.pin) == LOW) Serial.printf("%s reads LOW at boot: check its wiring (it should only go to GND when pushed)\n", b.name);
  }
  lastPress = millis();
}

void loop() {
  uint32_t now = millis();
  for (auto &b : buttons) {
    bool raw = digitalRead(b.pin) == LOW;
    if (raw != b.raw) {
      b.raw = raw;
      b.changedAt = now;
    }
    if (b.raw == b.down || now - b.changedAt < kDebounceMs) continue;
    b.down = b.raw;
    lastPress = now;
    if (b.down) {
      b.downAt = now;
      if (!backlight && lcd) {
        lcd->backlight();
        backlight = true;
        Serial.printf("%s: backlight on (wake only)\n", b.name);
        b.downAt = 0;  // this push only wakes the screen
      }
      continue;
    }
    if (!b.downAt) continue;
    uint32_t held = now - b.downAt;
    char line[17];
    if (held >= kHoldMs) {
      snprintf(line, sizeof line, "%s hold %.1fs", b.name, held / 1000.0);
    } else {
      b.presses++;
      snprintf(line, sizeof line, "%s press %lu", b.name, (unsigned long)b.presses);
    }
    Serial.print(line);
    if (!lcd) Serial.println();
    show(lcdAddr ? "Turkey caller" : "", line);
  }
  if (backlight && lcd && now - lastPress > kBacklightMs) {
    lcd->noBacklight();
    backlight = false;
    Serial.println("backlight off (30 s without a press)");
  }
  delay(5);
}
