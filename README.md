# Turkey Caller

An ESP32 that stores turkey, moose and deer calls and plays them on a Bluetooth speaker. Pick and play calls with the four buttons and the small screen on the box, or from your phone: the ESP32 serves its own web page.

- **Board:** Freenove ESP32-WROVER-E (4 MB flash, 8 MB PSRAM) with a microSD card. The older WROOM-32E is the spare.
- **On the box:** an LCD1602 screen (I2C) and four big buttons, all from the Freenove kit ([F15](docs/features/F15-on-device-ui.md)). No phone is needed for a hunt.
- **Phone → ESP32 (optional):** the ESP32 runs its own Wi-Fi network; the control page opens at `http://192.168.4.1`. Phone-free mode turns the Wi-Fi off.
- **ESP32 → speaker:** Bluetooth Classic A2DP ([ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP))
- **Sounds:** mono IMA ADPCM WAV files (22.05 kHz). Built-in calls in flash (LittleFS); call packs and phone recordings on the SD card. Decoded on the ESP32.

## Using the box

| Button | On the call list | While a call plays |
|---|---|---|
| **Yellow** | Previous call | Volume up |
| **Blue** | Next call | Volume down |
| **Green** | Play the call shown | Hold 1 s: Repeat on or off |
| **Red** | Next animal (Turkey, Moose, Deer). Hold 3 s: Wi-Fi off or on (phone-free mode) | Stop |
| **BOOT** (on the board) | Hold 5 s: with Wi-Fi off, turns it back on (name and password kept). With Wi-Fi on, resets the Wi-Fi name, password and device name to the factory values | Same |

The screen's backlight turns off after 30 s without a press. The first press of any button only wakes it.

Wiring for the screen and buttons: [firmware/README.md](firmware/README.md#the-box-screen-and-buttons-f15).

Full design: [docs/architecture.md](docs/architecture.md)

## Repo layout

| Path | What |
|---|---|
| `firmware/` | PlatformIO project (Arduino core 3.x via pioarduino) |
| `firmware/partitions.csv` | 2.5 MB app + 1.375 MB LittleFS |
| `firmware/tools/` | One-off board probes: `sd-probe` (SD card, PSRAM), `ui-probe` (screen and buttons) |
| `firmware/data/sounds/` | Generated sound files (not in git) + hand-written `index.json` |
| `web/` | Web app source and mock API server |
| `tools/audio/` | Sound download/convert script and source list |
| `docs/` | Architecture, feature specs, test plans |

## Getting the sounds

The recordings are downloaded, not stored in this repo, because most of them are not freely licensed.

```sh
brew install ffmpeg
cd tools/audio && ./fetch_turkey_audio.sh
rm -f ../../firmware/data/sounds/*.mp3
cp data/*.wav ../../firmware/data/sounds/
```

Sources and licenses are listed in `tools/audio/SOURCES.md`.

## Status

Working on Eric's WROVER-E board (31 Oct 2026): the phone page, speaker pairing and reconnect, Repeat, settings, the SD card with call packs and phone uploads (F12, F14, F03), and the box's screen and buttons with phone-free mode (F15). Feature specs and their board tests are in [docs/features/](docs/features/README.md). Flash and test steps: [firmware/README.md](firmware/README.md).
