// Turkey Caller: Wi-Fi softAP + control page + Bluetooth A2DP playback of turkey calls.
#include <Arduino.h>
#include <LittleFS.h>

#include "bt.h"
#include "heapmon.h"
#include "library.h"
#include "player.h"
#include "version.h"
#include "web.h"

// loop() only runs small housekeeping; the default 8 KB stack wastes RAM we need for audio.
SET_LOOP_TASK_STACK_SIZE(4 * 1024);

static void heapLog(const char *stage) {
  Serial.printf("heap after %-10s free=%u block=%u\n", stage, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nTurkey Caller " FW_VERSION);

  // true = format if the partition is blank, so the page still works before `uploadfs`.
  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  Serial.printf("LittleFS: %u of %u bytes used\n", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());

  library::begin();  // sound list from /sounds/index.json and the clip headers
  heapLog("boot");
  player::begin();
  heapLog("player");
  bt::begin();   // Bluetooth first, so it gets its memory before Wi-Fi takes its share
  heapLog("bluetooth");
  web::begin();
  heapLog("wifi+web");
  heapmon::begin();
  Serial.printf("Ready. Join Wi-Fi \"TurkeyCaller\" (password gobble123), open http://192.168.4.1\n");
}

void loop() {
  web::loop();
  player::loop();
  bt::loop();

  static uint32_t last = 0;
  if (millis() - last >= 5000) {
    last = millis();
    player::Stats p = player::stats();
    static uint32_t lastFills = 0;
    Serial.printf("[%lus] clients=%d req=%d bt=%s playing=%s plays=%u underruns=%u buf=%u fills=+%u heap=%u min=%u low=%u block=%u\n",
                  (unsigned long)(millis() / 1000), web::clients(), web::requestsInFlight(), bt::state(), p.playing ? p.current : "-",
                  (unsigned)p.plays, (unsigned)p.underruns, (unsigned)p.bufferedBytes,
                  (unsigned)(p.fills - lastFills), (unsigned)ESP.getFreeHeap(),
                  (unsigned)heapmon::minEver(), (unsigned)heapmon::lowSince(), (unsigned)ESP.getMaxAllocHeap());
    lastFills = p.fills;
  }
  delay(5);
}
