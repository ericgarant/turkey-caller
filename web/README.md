# Web app

The control page the ESP32 serves: **Calls**, **Speaker** and **Settings** screens, built for a phone in the woods. One file, `index.html`, with inline CSS and JS and system fonts only, so it needs no internet. Design mockup: https://claude.ai/artifact/CEv3yQKF57byhWBqGVYNv5

| Path | What |
|---|---|
| `index.html` | The page |
| `build.py` | Gzips the page into `firmware/src/web_page.h` (git-ignored) and fails over 20 KB |
| `mock/server.mjs` | Fake ESP32 API with no dependencies, so the page can be built and tested without the board. Mock-only hook: `POST /api/mock/speaker mode=asleep|off|on` puts the speaker to sleep or out of reach to test waking |
| `test/smoke.mjs` | Playwright run of the main flows against the mock at phone width. CI runs it on every change under `web/` |

## Use it

```sh
cd web
node mock/server.mjs        # then open http://localhost:8080 (phone width in dev tools)
npm install && npm test     # smoke test against the mock
python3 build.py            # writes firmware/src/web_page.h, then flash with pio run -t upload
```

## What the page does

- **Calls:** one button per sound, grouped Soft calls / Louder calls / Use with care (gobble and putt). Tap to play, tap the playing call again to stop. "Repeat" loops the call. Stop and volume (slider plus − / + steps for gloved hands) stay pinned at the bottom.
- **Speaker:** status, search (about 10 s, stops playback), Use, Forget (tap twice). Speakers without a name have no Use button until the firmware connects by address.
- **Settings:** sound list with storage meter, upload (asks before replacing a sound with the same name) and delete (refused while playing), Wi-Fi name and password, device name, and device health.
- **Dawn mode** (moon button): dim, red-only colors to keep night vision. Remembered on the phone. Otherwise the page follows the phone's light or dark setting.

## API contract with the firmware

Agreed with the firmware thread; the summary table is in `../docs/architecture.md`. POST bodies are form-encoded (`application/x-www-form-urlencoded`), replies are JSON, and errors are `{"ok":false,"error":"text"}` with 400, 404, 409 or 413.

| Call | Notes for the page |
|---|---|
| `GET /api/status` | `bt` is `idle`, `searching`, `connecting` or `connected`; also `speaker`, `playing`, `current`, `loop` (the call playing now repeats), `repeat` (the shared Repeat switch), `pos_ms`, `len_ms`, `volume`, `heap`, `minHeap`, `fsFree`, `fsTotal`, `clients`, `uptime`, `underruns`, `version`, `stream` (`ready`, `waking`, `suspended` or `down`) and `error` (last wake failure, such as "Speaker did not answer"; cleared by the next tap) |
| `GET /api/sounds` | `[{id, name, group, bytes, ms}]` in `sounds/index.json` order. `group` is `soft`, `loud` or `care`; sounds without one are shown under "Other sounds". A plain list of ids (Phase 0) also works |
| `POST /api/play` `id`, `loop=0/1` | Never waits. 200: playing now. 202 `{"waking":true}`: the speaker was asleep or out of reach, the board wakes it (or reconnects) and plays, or sets `error` after 8 s. 409: no speaker saved, or a search is running. Stop also cancels a waking call |
| `POST /api/stop`, `POST /api/volume` `level` | |
| `POST /api/repeat` `on=0/1` | Sets the Repeat switch on the board, shared by every phone; also changes whether the call playing now repeats. A play's `loop` sets it too. Off after a restart |
| `POST /api/bt/scan`, `GET /api/bt/devices` | Devices reply `{scanning, devices:[{name, addr, rssi}]}`; a bare array also works |
| `POST /api/bt/connect` `addr`, `name` | Saves the speaker and connects by `addr` without a restart; `name` is for display and may be empty (the address is shown then). 400 when both are empty |
| `POST /api/bt/forget` | Disconnects and clears the saved speaker, no restart; stops playback |
| `GET /api/config` → `ap_ssid`, `device_name`; `POST /api/config` adds `ap_pass` | Password is never returned; empty means unchanged. Saving restarts the board |
| `POST /api/sounds` multipart `file` | The page renames the file to `a-z0-9_` (max 24 chars) + `.wav`. 409 while playing, 413 when full |
| `POST /api/sounds/delete` `id` | 409 while playing |
| `WS /ws` | Board pushes the `/api/status` JSON on change, every 2 s while playing, else every 10 s. The page sends `{"cmd":"volume","level":N}` and `{"cmd":"repeat","on":true}` over it; play and stop go over HTTP so errors come back as status codes. Without the socket the page polls `/api/status` every 3 s |

## How the page reaches the board

The page is compiled into the firmware, not stored in LittleFS. `pio run -t uploadfs` rewrites the whole LittleFS partition, so a page kept there would wipe every sound uploaded from the phone each time the page changed. `build.py` turns the page into `firmware/src/web_page.h` (`kWebPageGz`, `kWebPageGzLen`, about 10 KB). The Phase 2 firmware will run it from a PlatformIO pre-build script (failing the build when it fails) and serve it at `/` with `Content-Encoding: gzip`, replacing the Phase 0 test page. Updating the page is then a normal `pio run -t upload`, and the sounds stay put.
