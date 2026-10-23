# Firmware

PlatformIO project for the Freenove ESP32-WROOM-32E. Phase 0 code goes here next.

- `partitions.csv`: 2 MB app, 1.875 MB LittleFS (`data/`), 64 KB coredump. No OTA.
- `data/sounds/` is filled from `tools/audio/data/` (see `tools/audio/fetch_turkey_audio.sh`), then uploaded with `pio run -t uploadfs`.
