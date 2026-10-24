// Turkey Caller, Phase 0: prove Wi-Fi softAP + web page + Bluetooth A2DP MP3 playback together.
#include <Arduino.h>
#include <LittleFS.h>

#include "bt.h"
#include "player.h"
#include "web.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nTurkey Caller Phase 0");

  // true = format if the partition is blank, so the page still works before `uploadfs`.
  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  Serial.printf("LittleFS: %u of %u bytes used\n", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());

  player::begin();
  bt::begin();   // Bluetooth first, so it gets its memory before Wi-Fi takes its share
  web::begin();
  Serial.printf("Ready. Join Wi-Fi \"TurkeyCaller\" (password gobble123), open http://192.168.4.1\n");
}

void loop() {
  web::loop();
  player::loop();

  static uint32_t last = 0;
  if (millis() - last >= 5000) {
    last = millis();
    player::Stats p = player::stats();
    Serial.printf("[%lus] bt=%s playing=%s plays=%u underruns=%u heap=%u min=%u block=%u\n",
                  (unsigned long)(millis() / 1000), bt::state(), p.playing ? p.current : "-",
                  (unsigned)p.plays, (unsigned)p.underruns, (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  }
  delay(5);
}
