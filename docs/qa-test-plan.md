# QA test plan

Board tests are run by hand on the real board and speaker. Each row is pass or fail; write results in the PR or issue for the build you tested (version from `/api/status`). Automated tests run in CI against the mock and do not need the board.

Test setup: ESP32 plugged into the Mac (power and serial log), speaker charged and 1 m away, phone with the page open. The board's Wi-Fi is the softAP (`TurkeyCaller`, page at `http://192.168.4.1`). Calls are IMA-ADPCM WAV at half sample rate; expect `.wav` names where older docs say `.mp3`.

## Automated (CI, every change under `web/`)

| Test | What it covers |
|---|---|
| `npm run test:api` | API contract on the mock: status fields, sound list shape, play/replace/stop, 400/404/409/413 errors, volume clamp, upload and delete rules, config validation, forget and reconnect |
| `npm test` (smoke) | Page flows in a phone-sized browser: 12 calls, play/stop, volume, speaker forget/search/use, upload/delete hidden (F03 deferred), Wi-Fi settings, dawn mode |
| `npm run test:features` | HTTP and WebSocket tests on the mock: every call plays, volume (HTTP, WebSocket, clamp, seen by both phones within 1 s), Repeat shared by two clients (late joiner, switching during a call, replace, Stop, forget), busy 503 recovery, and speaker wake and reconnect (F06/F07, active once the PR #14 mock hook is on main). Upload tests are parked (phone upload deferred, PR #12); `RUN_PARKED=1` runs them |
| `npm run test:phones` | Two phones in a browser: playing state, Repeat and volume stay in step; the page retries through a busy window and says so when the board stays busy |
| `npm run stress` | 50 rapid plays (150 ms apart) against the mock; same pass rule as the board test B9 |

The same scripts run against the board: `node web/test/stress.mjs http://192.168.4.1` and `node web/test/api.mjs http://192.168.4.1`. On the board `api.mjs` runs only read-only and play/stop/volume steps; `DESTRUCTIVE=1` adds forget, config, upload and delete (these change the board). It lists every mismatch at the end (for example the `bt` state strings the Phase 0 firmware reports, which differ from the page's `idle/searching/connecting/connected`). Any firmware change to the API must update the mock and `api.mjs` in the same PR.

What the mock cannot show, so it stays a board test: audio quality, real timing of retries (3 s / 10 s) and the 250 ms pre-roll, the 5 min idle pause, heap and underruns, Wi-Fi/Bluetooth sharing the radio, and a second tap keeping the first tap's 8 s deadline.

## Board tests (Eric, by hand)

Fast pass (about 10 minutes): B1, B2, B4, B5, B9. Full pass before any release.

**Which firmware:** the "Now" column is for the Phase 0 firmware (PR #3, its own test page). Tests marked "Phase 2" need the real page and API and will fail on Phase 0 for reasons that are not bugs; skip them until then.

| # | Now | Test | Steps | Pass if |
|---|---|---|---|---|
| B1 | Yes, except the version in the header (Phase 2) | Wi-Fi join | Power on, phone joins `TurkeyCaller`, open `http://192.168.4.1` (and let the captive page open if the phone offers it) | Page loads in under 5 s; Calls screen shows 12 calls; header shows the board version |
| B2 | Yes | Speaker pair | Speaker screen, Search (about 10 s), tap Use on the Level Box Slim | Status goes searching, connecting, connected; header shows the speaker name; speaker confirms (voice or light) |
| B3 | Yes | Speaker reconnect | a) Turn the speaker off and on. b) Walk the speaker out of range and back. c) Reboot the board with the speaker on | Board reconnects on its own within 30 s each time, with no taps; a call plays afterwards |
| B4 | Yes | Each call plays | Tap each of the 12 calls once: tree call, cluck, purr, cluck & purr, whine, plain yelp, assembly yelp, cutting, fly-down cackle, kee-kee run, gobble, putt | Every call is audible and recognizable, ends by itself at about its listed length, with no clicks, speed or pitch error, or silence; the button highlight clears at the end |
| B5 | Yes | Stop and replace | Start a long call, tap Stop; start one, tap another call; tap the playing call again | Stop silences within 0.5 s; a new call replaces the old one cleanly; tapping the playing call stops it |
| B6 | Phase 2 (no Repeat yet) | Repeat | Turn Repeat on, play a call for 2 minutes, then Stop | Loops with no gap growth or dropout; Stop works |
| B7 | Yes | Volume | Slider and the − / + buttons at 0, 30, 60, 100; change volume while a call plays | Level follows the control within 0.5 s and never jumps; 0 is silent; 100 is not distorted on the Level Box; value is kept after a reboot (wait 3 s with nothing playing before rebooting: the firmware saves volume only after about 2 s idle) |
| B8 | Yes | No speaker | Forget the speaker, then tap a call | Calls are disabled with a clear message; nothing crashes; re-pair works |
| B9 | Yes | Stress | From the Mac on the board's Wi-Fi: `node web/test/stress.mjs http://192.168.4.1` (50 taps, 150 ms apart, the same as the firmware page's own stress test). Also tap by hand as fast as you can for 30 s | Script prints PASS: 0 failed requests, 0 new underruns during the run, no reboot, lowest heap since boot >= 40 KB (40960). No reboot in the serial log; audio recovers after the last tap |
| B10 | Yes (Wi-Fi name check is Phase 2) | Power cycle | Pull power during playback, restore it; repeat 5 times | Board comes back with the same speaker, volume, Wi-Fi name and sounds; auto-reconnects; no filesystem error in the serial log |
| B11 | Yes | Range | Phone at 5, 15, 30 m from the board (open ground); then board-to-speaker at 3, 8, 12 m | Note the distance where the page gets slow or drops, and where audio breaks up; the page must recover on its own when you walk back. Targets: page usable at 15 m, audio clean at 8 m |
| B12 | Yes | Two phones | Join a second phone while a call plays | Audio does not break up; both pages show the same state; a third client is refused politely (max 2) |
| B13 | Deferred (F03) | Upload and delete | Upload a short clip from the phone while idle; try the same during playback; delete it | Idle upload works and plays; during playback it is refused with a message; existing sounds are untouched |
| B14 | Phase 2 | Settings change | Change the Wi-Fi name and password in Settings, save | Board restarts, new name appears, phone rejoins with the new password; a password under 8 characters is refused |
| B15 | Phase 2 | Page update keeps sounds | Flash a new firmware build with `pio run -t upload` (not `uploadfs`) | All sounds are still there |
| B16 | Yes, via the command | Soak (Phase 4) | Run `TAPS=360 GAP_MS=20000 node web/test/stress.mjs http://192.168.4.1` (a call every 20 s for 2 hours, in order, at normal volume) | Script prints PASS; no audible dropout; speaker stays connected |

## What to record each run

Firmware version, speaker, B-numbers passed and failed, and from `/api/status` after B9 and B16: `minHeap`, `underruns`, `uptime`. Paste the serial log around any failure. A failure becomes a GitHub issue labelled `qa` with the steps and the log.

## Open questions for the firmware thread

- Confirm the file extension and `bytes`/`ms` fields for WAV; the page and mock still rename uploads to `.mp3` (`web/index.html`, `web/mock/server.mjs`) and need a small update when the format is final.
- `underruns` must be a running count in `/api/status` (the stress pass rule reads it) and reset at boot only.
