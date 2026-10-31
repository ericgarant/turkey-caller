# Architecture

The illustrated version of this document lives at https://claude.ai/artifact/87pxFYgU6Ydde4tJB3pjFW. This file is the in-repo summary that code changes should keep up to date.

## Rules

These hold for every change. A PR that breaks one needs Eric's OK first, and then this list changes in the same PR.

**Hardware**
1. The main board is the Freenove ESP32-WROVER-E with the microSD card. The WROOM-32E is the spare; it runs the `wroom-final` branch, and `pio run -e wroom` must keep building in CI.
2. Only an original ESP32 (not S2, S3, C3 or C6): the speaker needs Bluetooth Classic A2DP, which only the original ESP32 has.
3. SD card in SD_MMC 1-bit mode on CLK 14, CMD 15, D0 2. Firmware never drives GPIO 12 (boot strap) or 13 (the slot's DAT3). The card is mounted once at boot; a failed read marks it gone until restart and never resets the board.
4. Keep it simple. A feature that adds hardware or a moving part needs a clear field need (the motion sensor and LED, F13, were dropped for this reason).

**Memory**
5. Internal heap stays above 40 KB while playing with two phones on the page (F10). Every firmware PR reports the internal heap low (`minHeap`, `low=`).
6. Audio buffers (the PCM ring and decode buffers) are internal RAM, never PSRAM: static, or `heap_caps_malloc(MALLOC_CAP_INTERNAL)`, checked at boot. Large non-audio lists (the sound list) go to PSRAM when the board has it.
7. The PSRAM cache workaround stays on; `CONFIG_ESP32_REV_MIN_3` panics this build at boot.
8. App binary under 2.25 MB; the page under 20 KB gzipped.

**Sounds**
9. One format on the board: mono IMA ADPCM WAV, 22.05 kHz. Anything else is converted before it reaches the board (scripts in `tools/audio/`, or the page for uploads).
10. Built-in calls live in LittleFS. Call packs (`/packs/<pack>/index.json` plus WAVs) and phone uploads (`/sounds/`) live on the SD card. Order: built-ins, packs alphabetically, uploads.
11. One id namespace across built-ins, packs and uploads; the first one listed wins. At most 64 sounds (`library::kMaxSounds`). Pack clips and uploads are at most 60 s.
12. Every `index.json` is written by a JSON writer (`json.dumps`), never by hand-built strings, and stays under 4 KB. Each pack has one script, which is the source of truth for its ids, names and hints; the docs mirror it.
13. No audio in git, ever, including converted clips. Sources are listed in `tools/audio/SOURCES.md`; the clips are fetched or converted locally.

**API and page**
14. One contract for the board and the mock (`web/mock/server.mjs`): a change to either changes both, with an api or smoke test.
15. JSON GET replies send `Cache-Control: no-store`. Shared state (settings `rev`, sound list `snd`) is pushed so both phones update on their own.

**Process**
16. The architect reviews and merges every PR (squash). Nothing is pushed straight to main without Eric's OK.
17. A firmware PR merges only after Eric's board test of its QA row passes, CI is green for both boards, and its results are in the PR body. Checks the test didn't cover are listed there and carried forward.
18. Specs live in `docs/features/`; a decision that changes one updates its spec in the same PR.

## Decisions

| Area | Choice |
|---|---|
| Phone ↔ ESP32 | Wi-Fi softAP + HTTP and WebSocket. Browsers cannot speak ESP-NOW. |
| ESP32 ↔ speaker | Bluetooth Classic A2DP, ESP32 as source (ESP32-A2DP) |
| Web app | Plain HTML/CSS/JS, gzipped and compiled into the firmware (`web/build.py` → `firmware/src/web_page.h`), so `uploadfs` never touches it and a page update never wipes uploaded sounds. No internet needed |
| Audio | Mono IMA ADPCM WAV, 22.05 kHz (~11 KB/s; the 11 clips are ~0.83 MB), upsampled to 44.1 kHz on the ESP32. Not MP3: on the board, the Helix MP3 decoder's ~30 KB of heap left the Bluetooth stack too little to keep streaming with Wi-Fi up and a phone on the page. ADPCM decoding needs no heap and almost no CPU; turkey calls have little energy above 8 kHz, so the 11 kHz bandwidth loses nothing that matters |
| Board | Main: Freenove ESP32-WROVER-E with 8 MB PSRAM and a 1 GB microSD card (from F12, 25 Oct 2026). Spare: Freenove ESP32-WROOM-32E (4 MB flash, no PSRAM), which runs the `wroom-final` branch |
| Storage | Built-in calls in flash; call packs and phone uploads on the SD card ([F12](features/F12-wrover-sd.md), [F14](features/F14-call-packs.md), [F03](features/F03-phone-upload.md)). `partitions.csv`: 2.5 MB factory app, 1.375 MB LittleFS, no OTA. The build fails if the app binary passes 2.25 MB (90% of its partition). The data partition is named and typed `spiffs` (the Arduino and PlatformIO default) but formatted as LittleFS (`board_build.filesystem = littlefs`), so plain `LittleFS.begin()` and `pio run -t uploadfs` work |
| Toolchain | PlatformIO + pioarduino (Arduino core 3.x) |

## Radio sharing

One 2.4 GHz radio is time-shared between Wi-Fi and BT Classic.

- Decode into a ring buffer feeding the A2DP data callback. Phase 0 uses 16 KB of mono PCM (~0.19 s). A 32 KB ring plus the MP3 decoder ran the heap out on the board with Wi-Fi and a connected speaker.
- The stock Arduino libraries left only ~13 KB of heap with Wi-Fi + A2DP (first board test). `firmware/platformio.ini` sets a `custom_sdkconfig`: Classic-only controller, BLE/HFP/SPP off, smaller BT task stacks and Wi-Fi buffers. AsyncTCP's task stack is cut to 6 KB.
- Start the BT controller in Classic-only mode (`btStartMode(BT_MODE_CLASSIC_BT)`) before the A2DP library starts, which releases the BLE memory.
- Keep Wi-Fi traffic small during playback; max 2 AP clients; CPU at 240 MHz.
- Bluetooth discovery interrupts audio, so it only runs on user request.

## Firmware modules

| Module | Job |
|---|---|
| `net` | softAP, captive DNS, optional STA mode for development |
| `web` | Static UI, REST API, WebSocket status (ESPAsyncWebServer) |
| `uploads` | Phone recordings to `/sounds/` on the SD card, one at a time, via a `.tmp` file checked before it is renamed. The page converts any recording to mono ADPCM before upload (F03) |
| `sdcard` | Mounts the card once at boot, free space, marks it gone after a failed read |
| `bt` | Scan, connect, remember, auto-reconnect (BluetoothA2DPSource) |
| `player` | Decode task, ring buffer, volume gain, mono→stereo, stop/loop/queue |
| `library` | The sound list: built-ins from `sounds/index.json` (in git), then SD packs, then uploads; one id namespace |
| `control` | The actions the page and the box share: play, stop, volume and the Repeat switch (F15) |
| `ui` | The box's LCD and four buttons (F15, `TC_HAS_UI`, WROVER only). Goes through `control`, like `web` |
| `config` | NVS via Preferences |

## API

| Call | Purpose |
|---|---|
| `GET /api/status` | Speaker state, now playing, volume, free heap, free storage |
| `GET /api/sounds` | Sound list |
| `POST /api/play {id, loop}` | Play; a new play replaces the current one |
| `POST /api/stop` | Stop |
| `POST /api/volume {level}` | 0–100, saved |
| `POST /api/bt/scan`, `GET /api/bt/devices` | Find speakers |
| `POST /api/bt/connect {addr}`, `POST /api/bt/forget` | Pair / forget |
| `GET/POST /api/config` | AP name, password, device name; live on every phone via `rev` (F05) |
| `POST /api/sounds` (multipart `file`), `POST /api/sounds/delete {id}` | Upload / remove (no path params: ESPAsyncWebServer regex routes cost ~150 KB of flash). Uploads go to the SD card; refused with 409 for a built-in or pack id, or while a card sound plays |
| `WS /ws` | Status pushes; also accepts play, stop, volume |

## Phases

0. **Prove the risky part** (ESP32 coder, QA): softAP + page + A2DP playing a call. Done when there are no dropouts during 50 rapid taps, free heap stays above 40 KB, and the app binary with all libraries linked fits its budget. Phase 0 measured 1.75 MB against the original 2 MB app partition, so the app partition was grown to 2.5 MB (LittleFS 1.375 MB; the clips need ~0.83 MB as ADPCM).
1. **Specs and sounds** (feature writer, architect): user stories, final catalog, audio manifest. Specs for the next phase are in `docs/features/`.
2. **Firmware core** (ESP32 coder): modules and API, native unit tests.
3. **Web app** (web designer/coder): build against the mock, then the board.
4. **Test and field trial** (QA): CI, Playwright against the mock, on-device checklist and 2-hour soak.

Flashing and serial monitoring run on the Mac the board is plugged into.

## Test hardware

- Speaker now: Samsung Level Box Slim (EO-SG930). Replacement planned: Cooboom 20 W.
- The clips are all-rights-reserved or personal and are never committed (rule 13).
