# Web app

The control page the ESP32 serves: **Calls**, **Speaker** and **Settings** screens, built for a phone in the woods. One file, `index.html`, with inline CSS and JS and system fonts only, so it needs no internet. Design mockup: https://claude.ai/artifact/CEv3yQKF57byhWBqGVYNv5

| Path | What |
|---|---|
| `index.html` | The page |
| `build.py` | Gzips the page into `firmware/src/web_page.h` (git-ignored) and fails over 26 KB |
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
- **Speaker:** status, search (up to about 25 s, stops playback), Use, Forget (tap twice). A speaker without a name is usable by its address.
- **Settings:** sound list with storage meter, Wi-Fi name and password, device name, and device health. Upload and delete are deferred (F03): the controls are hidden by `EDIT_SOUNDS = false` and the firmware answers 501. When enabled, upload asks before replacing a sound with the same name, and delete is refused while playing.
- **Instructions** (`?` button in the header): joining the Wi-Fi, pairing, playing, Settings, the 5-second BOOT reset and troubleshooting. Static text, about 1.1 KB gzipped.
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
| `GET /api/config` → `ap_ssid`, `device_name`, `default_pass`, `rev`; `POST /api/config` adds `ap_pass` and `rev` | Password is never returned; empty means unchanged. Stale `rev` → 409. Reply `restart` is true only for Wi-Fi name or password changes. Status carries `cfg` (the rev) so every phone reloads on a change; see F05 |
| WS `{"evt":"restart","reason":"settings"\|"reset","ssid"}` | Sent just before a settings restart or BOOT reset; the page shows the rejoin message |
| `POST /api/sounds` multipart `file`, optional `animal`, `name`, `hint` | The page renames the file to `a-z0-9_` (max 24 chars) + `.wav`. 409 while playing, 413 when full. F16: append `animal` (`turkey`, `moose` or `deer`), `name` and `hint` to the FormData **before** `file`, or the board doesn't see them. A field left out keeps its value (new upload: turkey, name from the id, no hint). Unknown animal: 400, nothing saved. `name` is cut to 32 bytes and `hint` to 47 bytes of UTF-8 (accented letters take 2), so cap the inputs to match |
| `POST /api/sounds/edit` `id`, any of `name`, `hint`, `animal` | F16: relabels an upload; only its sidecar `/sounds/<id>.json` (`{"name","animal","hint"}`) is rewritten, never the audio. Omitted fields are kept; an empty `name` goes back to the name from the id. Same checks as upload. 404 unknown id, 400 built-in or pack call or bad animal, 409 while that recording is still uploading, 503 no SD card. Works while a call plays. The status `snd` rev moves, so every phone reloads `/api/sounds` |
| `POST /api/sounds/delete` `id` | 409 while playing. Also removes the upload's sidecar (F16) |
| `WS /ws` | Board pushes the `/api/status` JSON on change, every 2 s while playing, else every 10 s. The page sends `{"cmd":"volume","level":N}` and `{"cmd":"repeat","on":true}` over it; play and stop go over HTTP so errors come back as status codes. Without the socket the page polls `/api/status` every 3 s |

## How the page reaches the board

The page is compiled into the firmware, not stored in LittleFS. `pio run -t uploadfs` rewrites the whole LittleFS partition, so a page kept there would wipe every sound uploaded from the phone each time the page changed. `build.py` turns the page into `firmware/src/web_page.h` (`kWebPageGz`, `kWebPageGzLen`, about 10 KB). The Phase 2 firmware will run it from a PlatformIO pre-build script (failing the build when it fails) and serve it at `/` with `Content-Encoding: gzip`, replacing the Phase 0 test page. Updating the page is then a normal `pio run -t upload`, and the sounds stay put.
