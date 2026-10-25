// F12 probe for the WROVER-E. Prints PSRAM, flash size and which SD wiring mounts, then repeats the report every 10 s.
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SD_MMC.h>
#include <SPI.h>
#include <esp_flash.h>

static String result;

static void report(fs::FS &fs, uint64_t cardSize, uint64_t total, uint64_t used, const char *mode) {
  result += String("MOUNTED ") + mode + "\n";
  result += "  card size MB: " + String((double)cardSize / 1048576.0, 1) + "\n";
  result += "  fs total MB:  " + String((double)total / 1048576.0, 1) + "\n";
  result += "  fs free MB:   " + String((double)(total - used) / 1048576.0, 1) + "\n";
  File f = fs.open("/probe.txt", FILE_WRITE);
  bool wrote = f && f.print("turkey caller probe\n") > 0;
  if (f) f.close();
  f = fs.open("/probe.txt");
  String back = f ? f.readString() : String();
  if (f) f.close();
  fs.remove("/probe.txt");
  result += String("  write/read: ") + (wrote && back.startsWith("turkey") ? "OK" : "FAILED") + "\n";
  File root = fs.open("/");
  result += "  root:";
  for (File e = root.openNextFile(); e; e = root.openNextFile()) result += String(" ") + e.name();
  result += "\n";
}

static bool tryMmc() {
  SD_MMC.setPins(14, 15, 2);
  if (!SD_MMC.begin("/sdcard", true /*1-bit*/)) { result += "SD_MMC 1-bit (CLK14 CMD15 D0 2): no mount\n"; return false; }
  if (SD_MMC.cardType() == CARD_NONE) { result += "SD_MMC: no card\n"; SD_MMC.end(); return false; }
  report(SD_MMC, SD_MMC.cardSize(), SD_MMC.totalBytes(), SD_MMC.usedBytes(), "SD_MMC 1-bit CLK14 CMD15 D0=2");
  return true;
}

static bool trySpi(int sck, int miso, int mosi, int cs) {
  static SPIClass spi(HSPI);
  spi.end();
  spi.begin(sck, miso, mosi, cs);
  String label = "SPI SCK" + String(sck) + " MISO" + String(miso) + " MOSI" + String(mosi) + " CS" + String(cs);
  if (!SD.begin(cs, spi, 4000000) || SD.cardType() == CARD_NONE) { SD.end(); result += label + ": no mount\n"; return false; }
  report(SD, SD.cardSize(), SD.totalBytes(), SD.usedBytes(), label.c_str());
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  uint32_t flash = 0;
  esp_flash_get_size(NULL, &flash);
  result += "chip: " + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + "\n";
  result += "flash MB: " + String(flash / 1048576) + "\n";
  result += "psram total KB: " + String(ESP.getPsramSize() / 1024) + ", free KB: " + String(ESP.getFreePsram() / 1024) + "\n";
  result += "internal heap free KB: " + String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024) + "\n";
  // GPIO 33 (LED) and 34 (PIR, F13) are never touched.
  if (!tryMmc()) {
    trySpi(18, 19, 23, 5);
    return;
  }
  // Wiring check only: a 4-bit mount works only if D1-D3 reach the slot, which shows GPIO 13
  // is the card's DAT3 (so nothing else may pull it low at card init). The firmware uses 1-bit.
  SD_MMC.end();
  SD_MMC.setPins(14, 15, 2, 4, 12, 13);
  bool four = SD_MMC.begin("/sdcard", false);
  result += String("4-bit check (D1=4 D2=12 D3=13): ") + (four ? "mounts, so GPIO 13 is the slot's DAT3" : "no mount") + "\n";
  SD_MMC.end();
}

void loop() {
  Serial.print("\n=== PROBE ===\n" + result + "=== END ===\n");
  delay(10000);
}
