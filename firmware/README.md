# Firmware

PlatformIO project for the Freenove ESP32-WROOM-32E (4 MB flash, no PSRAM).

**Phase 0** (this code): the ESP32 runs its own Wi-Fi network with a test page, and plays MP3 calls from LittleFS on a Bluetooth A2DP speaker. The goal is to prove Wi-Fi and Bluetooth audio work together before building the real app.

| File | Job |
|---|---|
| `src/main.cpp` | Boot order, serial health log every 5 s |
| `src/player.*` | Decode task (Helix MP3), 32 KB ring buffer, volume, A2DP data callback |
| `src/bt.*` | A2DP source: finds the saved speaker by name, remembers its address, reconnects |
| `src/web.*` | softAP `TurkeyCaller`, captive DNS, test page, JSON API |
| `page/index.html` | The test page. Gzipped into `src/page.h` at build time by `scripts/embed_page.py` |
| `scripts/check_size.py` | Fails the build if the app binary is over 2.25 MB (90% of the app partition) |
| `partitions.csv` | 2.5 MB app, 1.375 MB LittleFS (`data/`, partition labeled `spiffs` so defaults work), 64 KB coredump. No OTA |

## Flash it (Mac)

The board is plugged into the Mac by USB.

1. Install PlatformIO once: `brew install platformio` (or the PlatformIO extension in VS Code).
2. Get the sounds (needs `brew install ffmpeg`), from the repo root:
   ```sh
   cd tools/audio && ./fetch_turkey_audio.sh
   cp data/*.mp3 ../../firmware/data/sounds/
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
2. On a phone, join Wi-Fi **TurkeyCaller**, password **gobble123**. The page should pop up; if not, open `http://192.168.4.1`.
3. Under **Speaker**, tap **Search for speakers**. The ESP32 lists the speakers it finds; tap **Use** next to yours. The ESP32 saves the name, restarts, connects, and from then on reconnects to that speaker by itself.
4. Tap the call buttons. **Stop** and the **volume** slider act right away (volume is saved).
5. **Stress test: 50 taps** plays 50 calls 150 ms apart and reports failed taps, underruns, and the lowest free heap.

Phase 0 passes when the stress test shows **0 underruns** and no audible dropouts, and **lowest free heap stays above 40 KB**. To change speakers (for example the Cooboom later), tap **Forget speaker** and pick the new one.

## API (Phase 0 subset)

Parameters are form fields or query strings; replies are JSON.

| Call | Purpose |
|---|---|
| `GET /api/status` | Bluetooth state, speaker, now playing, volume, plays, underruns, buffer fill, free/min heap, free storage |
| `GET /api/sounds` | Sound names from `/sounds/*.mp3` |
| `POST /api/play` `id=cluck` | Play; a new play replaces the current one. 409 if no speaker is connected |
| `POST /api/stop` | Stop |
| `POST /api/volume` `level=0..100` | Software gain, saved in NVS |
| `GET /api/bt/devices` | Speakers seen while searching |
| `POST /api/bt/connect` `name=…` | Save the speaker name and restart |
| `POST /api/bt/forget` | Clear the speaker and restart |

## Known limits of Phase 0

- Bluetooth searching takes most of the shared radio, enough that phones can fail to join the Wi-Fi. So with no speaker saved, the ESP32 only searches after you tap **Search for speakers**, and it favours Wi-Fi while searching. With a saved speaker that is switched off, it keeps trying to reconnect, so turn the speaker on first.
- Clips must be 44.1 kHz (the A2DP rate). Other rates play at the wrong speed; the serial log warns once.
- The Wi-Fi name and password are fixed in `src/web.cpp`; they become settings in Phase 2.
