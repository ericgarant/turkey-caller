# Firmware

PlatformIO project for the Freenove ESP32-WROOM-32E (4 MB flash, no PSRAM).

The ESP32 runs its own Wi-Fi network, serves the control page from `web/`, and plays turkey calls (IMA ADPCM WAV) from LittleFS on a Bluetooth A2DP speaker. Phase 0 proved Wi-Fi and Bluetooth audio work together; the next-phase features are specified in `docs/features/`.

| File | Job |
|---|---|
| `src/main.cpp` | Boot order, serial health log every 5 s |
| `src/wav.*` | WAV header reader (format checks, data offset, length in ms) |
| `src/library.*` | Sound list: order, names and groups from `data/sounds/index.json`, lengths from the WAV headers |
| `src/player.*` | WAV/IMA ADPCM decode task, repeat, resampler to 44.1 kHz, 16 KB ring buffer, volume, A2DP data callback |
| `src/bt.*` | A2DP source: finds the saved speaker by name, remembers its address, reconnects |
| `src/web.*` | softAP `TurkeyCaller`, captive DNS, the control page, JSON API, status WebSocket `/ws` |
| `scripts/prebuild.py` | Before each build: runs `../web/build.py` (page → `src/web_page.h`, fails over 20 KB) and writes `src/version.h` from `git describe` |
| `scripts/check_size.py` | Fails the build if the app binary is over 2.25 MB (90% of the app partition) |
| `partitions.csv` | 2.5 MB app, 1.375 MB LittleFS (`data/`, partition labeled `spiffs` so defaults work), 64 KB coredump. No OTA |

## Flash it (Mac)

The board is plugged into the Mac by USB.

1. Install PlatformIO once: `brew install platformio` (or the PlatformIO extension in VS Code).
2. Get the sounds (needs `brew install ffmpeg`), from the repo root. For the Dindon pack, first copy the five Dindon MP3s into `tools/audio/local/` (see `tools/audio/SOURCES.md`):
   ```sh
   cd tools/audio && ./fetch_turkey_audio.sh
   rm -f ../../firmware/data/sounds/*.mp3 ../../firmware/data/sounds/*.wav   # drop old clips (kee_kee_run is no longer built in)
   cp data/*.wav ../../firmware/data/sounds/   # next to the committed index.json
   cd ../../firmware
   ```
3. Build and flash the firmware, then the sounds:
   ```sh
   pio run -t upload
   pio run -t uploadfs
   ```
   The first build downloads the toolchain and ESP-IDF, then recompiles the ESP-IDF libraries with the trimmed Bluetooth/Wi-Fi settings in `custom_sdkconfig`. That takes 10 minutes or more, once. The build prints `App binary: … of the 2.25 MB budget`.
4. Watch the serial log: `pio device monitor` (Ctrl+C to quit). Every 5 s it prints Bluetooth state, plays, underruns and free heap.

Uploads run at 460800 baud because 921600 fails on this board's CH340 chip under macOS. If upload can't find the board, list ports with `pio device list` and pass one, for example `pio run -t upload --upload-port /dev/cu.usbserial-110`. The Freenove board uses a CH340 USB chip; recent macOS has the driver built in. If it says "Failed to connect", hold the BOOT button while the upload starts.

## Connect the speaker and test

1. Turn the speaker on and put it in pairing mode (Samsung Level Box Slim: hold the Bluetooth button until it announces pairing).
2. On a phone, join Wi-Fi **TurkeyCaller**, password **gobble123** (the factory settings; change them under **Settings**). The page should pop up; if not, open `http://192.168.4.1`.
3. Under **Speaker**, tap **Search for speakers**. The ESP32 lists the speakers it finds; tap **Use** next to yours. The ESP32 saves it and connects by address (no restart), and from then on reconnects to that speaker by itself: every 3 s for the first minute it is gone, then every 10 s. A tap on a speaker that went to sleep wakes it first (the page says "Waking the speaker"); if it doesn't answer within 8 s the page says so.
4. Tap the call buttons. **Repeat** loops a call, **Stop** and the **volume** slider act right away (volume is saved).
5. From a computer on the board's Wi-Fi, `cd web && node test/stress.mjs http://192.168.4.1` taps 50 calls and reports failed taps, underruns and the lowest free heap, and `node test/api.mjs http://192.168.4.1` checks the API contract.

The page is part of the app binary, so `pio run -t upload` updates it and keeps the sounds. `uploadfs` rewrites the whole LittleFS partition (sounds and `index.json`). To change speakers (for example the Cooboom later), tap **Forget** and pick the new one.

## Sounds

Clips are `data/sounds/<id>.wav`, where the id is `a-z0-9_`, 24 characters at most. `data/sounds/index.json` (hand-written, in git) gives each id its place, display name and group (`soft`, `loud`, `care`, `pack`). `pack` is the Dindon pack: five calls cut from Eric's own MP3s (the built-in kee-kee run was dropped to make room) (purr & clucks, yelp, fast yelp, kee-kee run, gobble). Where each clip comes from and when to use each call is in `tools/audio/SOURCES.md`. Files that are not in the index are listed after the others with a name made from the id. A sound in the index without a file is not listed, and a broken file is skipped with a `sound … skipped: <why>` line on serial.

## API

The contract is in `../web/README.md`. POST bodies are form fields; replies are JSON. `/api/status` (and each `/ws` push) includes `version`, `minHeap`, `fsFree` and `fsTotal`. Not built yet, and answering 501: upload and delete (`POST /api/sounds`, `/api/sounds/delete`, F03).

## Known limits

- Bluetooth searching takes most of the shared radio, enough that phones can fail to join the Wi-Fi. So with no speaker saved, the ESP32 only searches after you tap **Search for speakers**, and it favours Wi-Fi while searching. Each search is one round of about 25 s (10 s setup, then ~13 s of inquiry), and the page can be slow or briefly unreachable during it; found speakers stay listed afterwards. Every device seen is printed on serial as a `saw …` line. With a saved speaker that is switched off, it keeps trying to reconnect, so turn the speaker on first.
- Clips must be mono WAV, IMA ADPCM or 16-bit PCM, at 8 to 44.1 kHz (resampled to the A2DP rate of 44.1 kHz). Anything else is refused with an error on serial.

## Settings (F05)

`src/settings.*` keeps the Wi-Fi name, password and device name in NVS (namespace `cfg`). The page's **Settings** screen changes them through `/api/config`. A device-name change applies at once on every phone (the Bluetooth name follows at the next restart); a Wi-Fi name or password change makes the board send `{"evt":"restart"}` to every page and restart about 1 s later. The password never leaves the board: not in replies, WebSocket messages or the serial log.

**Forgot the password?** With the board running, hold the **BOOT** button for 5 s. Serial prints `BOOT held 5 s: …`, the Wi-Fi name, password and device name go back to `TurkeyCaller` / `gobble123` / `Turkey Caller`, and the board restarts. The speaker, volume and sounds are kept. Holding BOOT while plugging in or pressing EN starts the chip's download mode instead, which is not this.
