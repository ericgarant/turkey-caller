# F12 Move to the WROVER-E board with an SD card

**Why:** Eric decided on 25 Oct 2026 that the Freenove ESP32-WROVER-E becomes the main caller. It has 8 MB of PSRAM and a microSD slot with a 1 GB card. The card gives room for hours of recordings (about 11 KB per second, so roughly 25 hours on 1 GB), which unblocks phone upload (F03) without touching the built-in calls. The PSRAM takes pressure off the internal heap (F10, issue #8). The old WROOM-32E board stays a spare with the firmware it has today.

## Storage layout

- **Built-in calls stay in flash (LittleFS)**, exactly as today: same partitions, same `index.json`, same fetch script and `uploadfs` step. The board plays them with no card inserted.
- **Phone uploads go to the SD card**, in `/sounds/` on a FAT-formatted card (FAT16 or FAT32; Eric's 1 GB card is fine as it comes).
- The sound list is the built-ins (in index order and groups, as today) followed by the card's sounds under "Your recordings". The ids share one namespace. An upload may not use a built-in id; that request gets 409 with "name already used by a built-in call".
- `/api/status` reports the card separately from flash: `sdFree` and `sdTotal`, both 0 when there is no card. `fsFree` and `fsTotal` stay for flash. The Settings storage meter shows the card, since that's where uploads go.
- **No card, or an unreadable card:** the board boots and plays the built-ins. The page shows "No SD card: recordings unavailable" in Settings and hides the add-a-sound field. Inserting a card needs a restart (no hot-plug).
- **A pulled or failing card is a bounded failure.** The call playing from it stops within about 1 s. The card is marked gone until the next restart, and read errors are logged at most once per second. `/api/status` `error` says "SD card removed", the same way #14 reports speaker errors. No crash, and the built-ins keep working.
- Uploads write to a temporary name on the card and rename on success, as F03 already says. Leftover temporary files are removed at boot.
- F03's "409 while a sound is playing" was meant to protect flash writes. On this board, an upload is allowed while a built-in plays. It is refused with 409 only while a sound from the card plays, because both use the card.

## SD wiring (first step: confirm on the board)

The pins depend on the Freenove board revision. Before anything else, the firmware thread flashes a small probe that tries SD_MMC 1-bit mode (CLK 14, CMD 15, D0 2) first, then SPI mode, and prints which one mounted, plus the card size and the free space. It then records the working mode and pins in `firmware/README.md`. Use 1-bit SD_MMC if it works: it avoids GPIO 4 and 12, and GPIO 12 is a boot strapping pin. The probe also prints the flash size with `esptool.py flash_id`, which the partition table must match.

GPIO 16 and 17 are taken by the PSRAM on WROVER modules, so nothing else may use them. GPIO 2 and 15 are strapping pins: if flashing fails with a card inserted, take the card out to flash, and write that down in the README. The probe also prints the chip revision.

Note the power source in the PR (Mac USB or a power bank). SD writes plus Wi-Fi and Bluetooth draw current peaks, so a brownout reset during the tests needs to be traceable to the supply.

## PSRAM

- Turn it on (`BOARD_HAS_PSRAM`, `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_USE_MALLOC=y`). The WROVER-E is chip revision 3, so set `CONFIG_ESP32_REV_MIN_3=y` instead of the old `-mfix-esp32-psram-cache-issue` workaround. If the probe reports an older revision, keep the workaround.
- Let Wi-Fi and lwIP allocate from PSRAM (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`).
- **Audio buffers stay internal, explicitly.** With `SPIRAM_USE_MALLOC`, a plain `malloc` of 16 KB or more goes to PSRAM, so the PCM ring and the decode buffers must be static arrays or `heap_caps_malloc(..., MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`. Check them at boot with `esp_ptr_internal()` and log a line saying so. The Bluetooth controller is internal anyway.
- Leave `CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST` off in this PR. Moving the Bluedroid host to PSRAM is a later, measured try for #8, and it lands only if underruns stay at 0.
- Leave the trimmed sdkconfig choices in place. Raise a Wi-Fi buffer count back up only if a measurement shows it helps.
- **F10 on this board:** the 40 KB floor applies to *internal* heap (`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`). `/api/status` adds `psramFree`. `minHeap` and the `low=` serial value stay internal-only, so the numbers on #8 stay comparable.

## Build

- A new PlatformIO env `wrover` (board `esp-wrover-kit` or `esp32dev` with the PSRAM flags) becomes the default env. The README flash steps use it.
- The old env stays as `wroom`, built in CI only, so the spare board can be reflashed. The branch `wroom-final` (main at 6a75dc5, with the instructions page) is the known-good build for the spare; nothing is merged into it.
- `check_size.py` keeps its 2.25 MB budget, unless the probe shows the WROVER-E has more than 4 MB of flash.

## Acceptance criteria (new QA row B18)

1. The probe's mode, pins, card size and flash size are in the PR and in `firmware/README.md`.
2. With the card in, all 16 built-ins play and `sdFree`/`sdTotal` are right. With the card out, the board boots, the built-ins play, and the page shows the no-card message.
3. A WAV copied to `/sounds/` on the card from the Mac shows up under "Your recordings" after a restart and plays without underruns, including on Repeat for 2 minutes.
4. F10 on the internal heap: B9 (50 rapid taps) and the two-phone tests from #8 stay at 40 KB or more, outside the transient events. Report `low=` and `psramFree` next to the old board's numbers on #8.
5. Pulling the card while a recording plays: that call stops with a toast, with no reset and no `[E]` loop. Built-ins still play afterwards.
6. Everything that passed on the old board still passes (B1 to B17 fast pass) on the new one.
