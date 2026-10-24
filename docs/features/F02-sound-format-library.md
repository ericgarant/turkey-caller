# F02 Sound format and library

**Story:** As a hunter, I tap a call and it sounds right, ends when it should, and I can see how long each one is.

## Format (decided)

- Mono IMA ADPCM WAV (`WAVE_FORMAT_IMA_ADPCM`, 4 bits per sample), **22.05 kHz**, half the A2DP rate of 44.1 kHz, about 11 KB per second. Clip files end in `.wav`.
- The firmware upsamples to 44.1 kHz; 16-bit PCM WAV (8 to 44.1 kHz) stays accepted as in Phase 0, for development only.
- The file names are the ids: lowercase `a-z0-9_`, at most 24 characters, for example `tree_call.wav`.
- `tools/audio/fetch_turkey_audio.sh` produces the 12 clips in this format; the clips are never committed.

## Library

- `sounds/index.json` (committed, in `firmware/data/sounds/`) gives order, display name and `group` (`soft`, `loud`, `care`) per id. Files without an entry are listed under "Other sounds", after the ones in the index, in upload order.
- `GET /api/sounds` returns `[{id, name, group, bytes, ms}]`. `ms` comes from the WAV header (samples per block times blocks), not from a guess. `bytes` is the file size.
- A sound in the index whose file is missing is not listed. A broken file (bad header, truncated) is not listed and is logged on serial; it never crashes the board and never blocks other sounds.
- Uploaded sounds are added to the list by the firmware (index entries for uploads are kept in a small second file on LittleFS, so `index.json` stays hand-written and `uploadfs` can replace it without losing uploads' names: if uploads are lost by `uploadfs` that is expected and documented in the firmware README).

## Acceptance criteria

1. All 12 calls play at the right speed and pitch, end at their listed length plus or minus 0.2 s (B4).
2. `ms` in `/api/sounds` is within 0.1 s of the length measured with ffprobe for every clip.
3. A corrupt file dropped in `/sounds` leaves the other sounds listed and playable, and the serial log names the bad file.
4. The page and the mock show `.wav` everywhere (`web/index.html`, `web/mock/server.mjs`, `web/test/api.mjs` updated in the same PR as the firmware).
5. The 12 clips total no more than the size in the fetch script's report, and the firmware reports `fsFree` and `fsTotal` correctly after boot, upload and delete.
