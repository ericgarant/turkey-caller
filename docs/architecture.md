# Architecture

The illustrated version of this document lives at https://claude.ai/artifact/87pxFYgU6Ydde4tJB3pjFW. This file is the in-repo summary that code changes should keep up to date.

## Decisions

| Area | Choice |
|---|---|
| Phone ↔ ESP32 | Wi-Fi softAP + HTTP and WebSocket. Browsers cannot speak ESP-NOW. |
| ESP32 ↔ speaker | Bluetooth Classic A2DP, ESP32 as source (ESP32-A2DP) |
| Web app | Plain HTML/CSS/JS, gzipped and compiled into the firmware (`web/build.py` → `firmware/src/web_page.h`), so `uploadfs` never touches it and a page update never wipes uploaded sounds. No internet needed |
| Audio | Mono IMA ADPCM WAV, 22.05 kHz (~11 KB/s; the 11 clips are ~0.83 MB), upsampled to 44.1 kHz on the ESP32. Not MP3: on the board, the Helix MP3 decoder's ~30 KB of heap left the Bluetooth stack too little to keep streaming with Wi-Fi up and a phone on the page. ADPCM decoding needs no heap and almost no CPU; turkey calls have little energy above 8 kHz, so the 11 kHz bandwidth loses nothing that matters |
| Storage | `partitions.csv`: 2.5 MB factory app, 1.375 MB LittleFS, no OTA. The build fails if the app binary passes 2.25 MB (90% of its partition). The data partition is named and typed `spiffs` (the Arduino and PlatformIO default) but formatted as LittleFS (`board_build.filesystem = littlefs`), so plain `LittleFS.begin()` and `pio run -t uploadfs` work |
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
| `web` | Static UI, REST API, WebSocket status, sound upload (ESPAsyncWebServer). Upload is deferred (F03): its routes answer 501 for now. When it returns: the device plays only mono ADPCM/PCM WAV, so the page converts other formats in the browser before upload (decodeAudioData + a small JS ADPCM encoder), or the upload is refused with a clear error |
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
| `GET/POST /api/config` | AP name, password, device name; live on every phone via `rev` (F05) |
| `POST /api/sounds` (multipart `file`), `POST /api/sounds/delete {id}` | Upload / remove (no path params: ESPAsyncWebServer regex routes cost ~150 KB of flash). Refused with 409 while audio is playing (flash writes stall the decoder's flash reads) |
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
- The downloaded clips are all-rights-reserved and are never committed; fetch them with `tools/audio/fetch_turkey_audio.sh`.
