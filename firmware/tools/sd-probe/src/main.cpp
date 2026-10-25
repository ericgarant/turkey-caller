// F12 probe for the WROVER-E. Prints PSRAM, flash size and which SD wiring mounts, then repeats the report every 10 s.
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SD_MMC.h>
#include <SPI.h>
#include <esp_flash.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>

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
#ifndef PROBE_FORMAT
#define PROBE_FORMAT false  // build with -DPROBE_FORMAT=true to format a card that has no file system
#endif
  if (!SD_MMC.begin("/sdcard", true /*1-bit*/, PROBE_FORMAT)) { result += "SD_MMC 1-bit (CLK14 CMD15 D0 2): no mount\n"; return false; }
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

// Talks to the card without a file system: does it answer in SD mode, and what is in sector 0?
static void rawCard() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = GPIO_NUM_14; slot.cmd = GPIO_NUM_15; slot.d0 = GPIO_NUM_2;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  if (sdmmc_host_init() != ESP_OK) { result += "raw: host init failed\n"; return; }
  sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot);
  static sdmmc_card_t card;
  esp_err_t e = sdmmc_card_init(&host, &card);
  if (e != ESP_OK) { result += "raw: card did not answer in SD mode (" + String(esp_err_to_name(e)) + ")\n"; sdmmc_host_deinit(); return; }
  uint64_t bytes = (uint64_t)card.csd.capacity * card.csd.sector_size;
  result += "raw: card answers in SD mode, " + String((double)bytes / 1048576.0, 0) + " MB, name " + String(card.cid.name) + "\n";
  static uint8_t sec[512];
  if (sdmmc_read_sectors(&card, sec, 0, 1) == ESP_OK) {
    char line[160];
    snprintf(line, sizeof(line), "raw: sector0 sig=%02x%02x part1 type=0x%02x start=%u size=%u, oem='%.8s' fs='%.8s'/'%.8s'\n",
             sec[510], sec[511], sec[0x1C2], (unsigned)(sec[0x1C6] | sec[0x1C7] << 8 | sec[0x1C8] << 16 | sec[0x1C9] << 24),
             (unsigned)(sec[0x1CA] | sec[0x1CB] << 8 | sec[0x1CC] << 16 | sec[0x1CD] << 24), sec + 3, sec + 0x36, sec + 0x52);
    result += line;
  } else {
    result += "raw: sector 0 read failed\n";
  }
  sdmmc_host_deinit();
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
  // GPIO 33 (LED) is never touched; GPIO 13 is only read.
  // Eric's PIR output is soldered to GPIO 13, the slot's DAT3. A card that sees DAT3 low at its
  // first command may switch to SPI mode, so log GPIO 13 (read only, never driven) around each try.
  pinMode(13, INPUT);
  result += "GPIO 13 (PIR) at boot: " + String(digitalRead(13) ? "high" : "low") + "\n";
  rawCard();
  if (tryMmc()) return;
  trySpi(18, 19, 23, 5);
}

void loop() {
  static uint32_t highs = 0, last = 0;
  static int prev = 0;
  int now = digitalRead(13);
  if (now && !prev) highs++;
  prev = now;
  if (millis() - last > 10000) {
    last = millis();
    Serial.print("\n=== PROBE ===\n" + result + "PIR on GPIO 13 now " + (now ? "high" : "low") + ", went high " + String(highs) + " times since boot\n=== END ===\n");
  }
  delay(10);
}
