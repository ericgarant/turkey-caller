# F03 Phone upload with conversion in the page

**Story:** As a hunter, I pick a call recording from my phone (usually MP3 or M4A) and it becomes a button, without a computer.

## Behavior

- The Settings screen file picker accepts any audio the phone browser can decode (`audio/*`: MP3, M4A/AAC, WAV, OGG). It is no longer limited to `.mp3`.
- The page converts on the phone before upload: decode (`decodeAudioData`), mix to mono, resample to 22.05 kHz (`OfflineAudioContext`), encode mono IMA ADPCM, wrap in a WAV header (standard block size of 256 bytes). The encoder is plain JS and adds well under 2 KB gzipped to the page (the 20 KB budget in `web/build.py` still holds).
- Before converting, the page shows the converted size and length. If it will not fit in the free room (see F09, keeping a 32 KB reserve), it says so and does not upload. A cap of 60 s per clip is applied by trimming with a warning, so one long recording cannot fill the board.
- Optional volume match: peak-normalize the converted clip to about 90% so uploads are not much quieter than the built-ins.
- The id comes from the file name (`a-z0-9_`, at most 24 characters, `.wav`). Same-name upload asks before replacing, as today.
- Progress shows converting, then uploading, then done. A failure shows the board's error text.

## Firmware rules

- `POST /api/sounds` (multipart `file`) checks the header. It refuses anything that is not mono IMA ADPCM or PCM WAV in range with 400 and a clear error; 409 while a sound is playing; 413 when it will not fit.
- The file streams to flash in chunks and never sits whole in RAM. The upload writes to a temporary name and renames on success, so a dropped connection or power cut never leaves a half sound in the list. Leftover temporary files are removed at boot.
- Heap stays at or above 40 KB during the upload (F10).

## Acceptance criteria

1. Upload a 20 s MP3 from an iPhone and an Android phone; it appears, plays at the right speed, and sounds like the original at 11 kHz bandwidth (B13).
2. Upload during playback is refused with a readable message and the playing call is not interrupted.
3. Pulling the phone off Wi-Fi in the middle of an upload leaves no new sound and no loss of free space after the next boot.
4. Uploading with too little room is refused before any data is sent (page check), and still refused with 413 if the board disagrees.
5. Replacing an existing sound keeps the old one until the new one is complete.
6. A file the phone cannot decode shows "This phone cannot read that file", and nothing is sent.
7. The mock and `web/test/api.mjs` check the WAV rules (400 for MP3 body, 409, 413) and the smoke test uploads a WAV fixture.
