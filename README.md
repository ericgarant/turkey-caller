# Turkey Caller

An ESP32 that stores turkey calls and plays them on a Bluetooth speaker. The ESP32 serves its own web page, so your phone is the remote.

- **Board:** Freenove ESP32-WROOM-32E (4 MB flash, no PSRAM)
- **Phone → ESP32:** the ESP32 runs its own Wi-Fi network; the control page opens at `http://192.168.4.1`
- **ESP32 → speaker:** Bluetooth Classic A2DP ([ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP))
- **Sounds:** mono MP3s in a LittleFS partition, decoded on the ESP32

Full design: [docs/architecture.md](docs/architecture.md)

## Repo layout

| Path | What |
|---|---|
| `firmware/` | PlatformIO project (Arduino core 3.x via pioarduino) |
| `firmware/partitions.csv` | 2 MB app + 1.875 MB LittleFS |
| `firmware/data/sounds/` | Generated sound files (not in git) + hand-written `index.json` |
| `web/` | Web app source and mock API server |
| `tools/audio/` | Sound download/convert script and source list |
| `docs/` | Architecture, feature specs, test plans |

## Getting the sounds

The recordings are downloaded, not stored in this repo, because most of them are not freely licensed.

```sh
brew install ffmpeg
cd tools/audio && ./fetch_turkey_audio.sh
cp data/*.mp3 ../../firmware/data/sounds/
```

Sources and licenses are listed in `tools/audio/SOURCES.md`.

## Status

Phase 0 (prove Bluetooth audio + Wi-Fi together on the board) is next. See the phased plan in the architecture doc.
