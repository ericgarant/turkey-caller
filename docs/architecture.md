# Architecture

The illustrated version of this document lives at https://claude.ai/artifact/87pxFYgU6Ydde4tJB3pjFW. This file is the in-repo summary that code changes should keep up to date.

## Decisions

| Area | Choice |
|---|---|
| Phone ↔ ESP32 | Wi-Fi softAP + HTTP and WebSocket. Browsers cannot speak ESP-NOW. |
| ESP32 ↔ speaker | Bluetooth Classic A2DP, ESP32 as source (ESP32-A2DP) |
| Web app | Plain HTML/CSS/JS, gzipped and compiled into the firmware (`web/build.py` → `firmware/src/web_page.h`), so `uploadfs` never touches it and a page update never wipes uploaded sounds. No internet needed |
| Audio | Mono MP3, 44.1 kHz, 64 kbps (~8 KB/s, ~3.5 min fits). Helix decoder from arduino-audio-tools |
| Storage | `partitions.csv`: 2 MB factory app, 1.875 MB LittleFS, no OTA. The data partition is named and typed `spiffs` (the Arduino and PlatformIO default) but formatted as LittleFS (`board_build.filesystem = littlefs`), so plain `LittleFS.begin()` and `pio run -t uploadfs` work |
| Toolchain | PlatformIO + pioarduino (Arduino core 3.x) |

## Radio sharing

One 2.4 GHz radio is time-shared between Wi-Fi and BT Classic.

- Release BLE memory at boot: `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)`.
- Decode into a ~0.5 s ring buffer feeding the A2DP data callback.
- Keep Wi-Fi traffic small during playback; max 2 AP clients; CPU at 240 MHz.
- Bluetooth discovery interrupts audio, so it only runs on user request.

## Firmware modules

| Module | Job |
|---|---|
| `net` | softAP, captive DNS, optional STA mode for development |
| `web` | Static UI, REST API, WebSocket status, MP3 upload (ESPAsyncWebServer) |
| `bt` | Scan, connect, remember, auto-reconnect (BluetoothA2DPSource) |
| `player` | Decode task, ring buffer, volume gain, mono→stereo, stop/loop/queue |
| `library` | List/add/delete sounds; names and order from `sounds/index.json` (hand-written, kept in git) |
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
| `GET/POST /api/config` | AP name, password, device name |
| `POST /api/sounds` (multipart `file`), `POST /api/sounds/delete {id}` | Upload / remove (no path params: ESPAsyncWebServer regex routes cost ~150 KB of flash). Refused with 409 while audio is playing (flash writes stall the decoder's flash reads) |
| `WS /ws` | Status pushes; also accepts play, stop, volume |

## Phases

0. **Prove the risky part** (ESP32 coder, QA): softAP + page + A2DP playing an MP3. Done when there are no dropouts during 50 rapid taps, free heap stays above 40 KB, and the app binary with all libraries linked is under 1.8 MB (if not, move space from LittleFS to the app partition; the clips need only ~0.5 MB).
1. **Specs and sounds** (feature writer, architect): user stories, final catalog, audio manifest.
2. **Firmware core** (ESP32 coder): modules and API, native unit tests.
3. **Web app** (web designer/coder): build against the mock, then the board.
4. **Test and field trial** (QA): CI, Playwright against the mock, on-device checklist and 2-hour soak.

Flashing and serial monitoring run on the Mac the board is plugged into.

## Test hardware

- Speaker now: Samsung Level Box Slim (EO-SG930). Replacement planned: Cooboom 20 W.
- The downloaded clips are all-rights-reserved and are never committed; fetch them with `tools/audio/fetch_turkey_audio.sh`.
