# QA test plan

Board tests are run by hand on the real board and speaker. Each row is pass or fail; write results in the PR or issue for the build you tested (version from `/api/status`). Automated tests run in CI against the mock and do not need the board.

Test setup: ESP32 plugged into the Mac (power and serial log), speaker charged and 1 m away, phone with the page open. The board's Wi-Fi is the softAP (`TurkeyCaller`, page at `http://192.168.4.1`). Calls are IMA-ADPCM WAV at half sample rate; expect `.wav` names where older docs say `.mp3`.

## Automated (CI, every change under `web/`)

| Test | What it covers |
|---|---|
| `npm run test:api` | API contract on the mock: status fields, sound list shape, play/replace/stop, 400/404/409/413 errors, volume clamp, upload and delete rules, config validation, forget and reconnect |
| `npm test` (smoke) | Page flows in a phone-sized browser: 12 calls, play/stop, volume, speaker forget/search/use, upload/delete, Wi-Fi settings, dawn mode |
| `npm run stress` | 50 rapid plays against the mock; same pass rule as the board test B9 |

When firmware lands, `npm run stress -- http://192.168.4.1` runs the identical script against the board, and `node mock/server.mjs` stays the reference for the API contract. Any firmware change to the API must update the mock and `api.mjs` in the same PR.

## Board tests (Eric, by hand)

Fast pass (about 10 minutes): B1, B2, B4, B5, B9. Full pass before any release.

| # | Test | Steps | Pass if |
|---|---|---|---|
| B1 | Wi-Fi join | Power on, phone joins `TurkeyCaller`, open `http://192.168.4.1` (and let the captive page open if the phone offers it) | Page loads in under 5 s; Calls screen shows 12 calls; header shows the board version |
| B2 | Speaker pair | Speaker screen, Search (about 10 s), tap Use on the Level Box Slim | Status goes searching, connecting, connected; header shows the speaker name; speaker confirms (voice or light) |
| B3 | Speaker reconnect | a) Turn the speaker off and on. b) Walk the speaker out of range and back. c) Reboot the board with the speaker on | Board reconnects on its own within 30 s each time, with no taps; a call plays afterwards |
| B4 | Each call plays | Tap each of the 12 calls once: tree call, cluck, purr, cluck & purr, whine, plain yelp, assembly yelp, cutting, fly-down cackle, kee-kee run, gobble, putt | Every call is audible and recognizable, ends by itself at about its listed length, with no clicks, speed or pitch error, or silence; the button highlight clears at the end |
| B5 | Stop and replace | Start a long call, tap Stop; start one, tap another call; tap the playing call again | Stop silences within 0.5 s; a new call replaces the old one cleanly; tapping the playing call stops it |
| B6 | Repeat | Turn Repeat on, play a call for 2 minutes, then Stop | Loops with no gap growth or dropout; Stop works |
| B7 | Volume | Slider and the − / + buttons at 0, 30, 60, 100; change volume while a call plays | Level follows the control within 0.5 s and never jumps; 0 is silent; 100 is not distorted on the Level Box; value is kept after a reboot |
| B8 | No speaker | Forget the speaker, then tap a call | Calls are disabled with a clear message; nothing crashes; re-pair works |
| B9 | Stress | From the Mac on the board's Wi-Fi: `node web/test/stress.mjs http://192.168.4.1` (50 taps, 250 ms apart). Also tap by hand as fast as you can for 30 s | Script prints PASS: 0 failed, 0 underruns, lowest heap >= 40 KB (40960). No reboot in the serial log; audio recovers after the last tap |
| B10 | Power cycle | Pull power during playback, restore it; repeat 5 times | Board comes back with the same speaker, volume, Wi-Fi name and sounds; auto-reconnects; no filesystem error in the serial log |
| B11 | Range | Phone at 5, 15, 30 m from the board (open ground); then board-to-speaker at 3, 8, 12 m | Note the distance where the page gets slow or drops, and where audio breaks up; the page must recover on its own when you walk back. Targets: page usable at 15 m, audio clean at 8 m |
| B12 | Two phones | Join a second phone while a call plays | Audio does not break up; both pages show the same state; a third client is refused politely (max 2) |
| B13 | Upload and delete | Upload a short clip from the phone while idle; try the same during playback; delete it | Idle upload works and plays; during playback it is refused with a message; existing sounds are untouched |
| B14 | Settings change | Change the Wi-Fi name and password in Settings, save | Board restarts, new name appears, phone rejoins with the new password; a password under 8 characters is refused |
| B15 | Page update keeps sounds | Flash a new firmware build with `pio run -t upload` (not `uploadfs`) | All sounds are still there |
| B16 | Soak (Phase 4) | Auto-play a random call every 20 s for 2 hours at normal volume | No dropout or reboot; heap `minHeap` stays >= 40 KB; underruns stay 0; speaker stays connected |

## What to record each run

Firmware version, speaker, B-numbers passed and failed, and from `/api/status` after B9 and B16: `minHeap`, `underruns`, `uptime`. Paste the serial log around any failure. A failure becomes a GitHub issue labelled `qa` with the steps and the log.

## Open questions for the firmware thread

- Confirm the file extension and `bytes`/`ms` fields for WAV; the page and mock still rename uploads to `.mp3` (`web/index.html`, `web/mock/server.mjs`) and need a small update when the format is final.
- `underruns` must be a running count in `/api/status` (the stress pass rule reads it) and reset at boot only.
