# Feature specs: next phase

Specs for what comes after Phase 0 (the Wi-Fi test page and Bluetooth ADPCM playback in PR #3, the control page in PR #4, the QA plan in PR #5). Each spec says what the hunter sees, how it must behave, and the acceptance criteria that QA turns into tests (`docs/qa-test-plan.md`). They do not say how to code it; the ESP32 coder and the web coder choose that, within the constraints in `docs/architecture.md`.

## Specs

| # | Spec | Who builds it | Test rows |
|---|---|---|---|
| F01 | [Firmware serves the built-in page](F01-built-in-page.md) | Firmware | B1, B15 |
| F02 | [Sound format and library](F02-sound-format-library.md) | Firmware, web | B4, B10 |
| F03 | [Phone upload with conversion in the page](F03-phone-upload.md) | Firmware, web (after F12) | B13 |
| F04 | [Repeat](F04-repeat.md) | Firmware, web | B6 |
| F05 | [Settings: Wi-Fi and device name](F05-settings.md) | Firmware, web | B14, B10 |
| F06 | [Speaker forget and reconnect](F06-speaker-pairing.md) | Firmware, web | B2, B3, B8 |
| F07 | [Idle speaker stream recovery](F07-idle-stream-recovery.md) | Firmware | new B17 |
| F08 | [Volume persistence](F08-volume-persistence.md) | Firmware | B7, B10 |
| F09 | [More room for sounds](F09-storage.md) | **Superseded by F12** | B13 |
| F10 | [Memory and stability floor](F10-heap-floor.md) | Everyone (cross-cutting) | B9, B16 |
| F11 | [Future Cooboom speaker](F11-cooboom-speaker.md) | Firmware, QA | B2 to B12 rerun |
| F12 | [Move to the WROVER-E board with an SD card](F12-wrover-sd.md) | Firmware, web | new B18 |

## Suggested build order

1. F10 stays in force the whole time. Every PR that touches firmware reports `minHeap` from `/api/status`.
2. F01 and F02 together (real page on the board, WAV names and sizes in the sound list).
3. F08, F04 (small, and the page already has the controls).
4. F06, F07 (the speaker work; do F07 right after F06 so reconnect and recovery share one state machine).
5. F05.
6. F12: move to the WROVER-E and its SD card (Eric's decision, 25 Oct 2026).
7. F03 on top of F12: uploads go to the SD card.
8. F11 when the Cooboom arrives.

## Deferred, then resumed

Eric skipped uploading his own recordings on 24 Oct 2026 and asked for them back on 25 Oct 2026. The old board's flash had only 32 KB left with 16 calls, so on the same day he chose the Freenove ESP32-WROVER-E with a 1 GB SD card as the main caller (F12).

- **F03 Phone upload** is active again and comes after F12. Uploads are stored on the SD card. Until F03 lands, the firmware keeps answering `POST /api/sounds` and `POST /api/sounds/delete` with `501`, and the page keeps `EDIT_SOUNDS = false` in `web/index.html`.
- **F09 More room for sounds** is superseded. The partition and trimming work isn't needed, because the SD card holds the uploads. The old board keeps its layout.

## Decisions for Eric

Defaults are written into the specs so work is not blocked; change them here and the specs follow.

1. **Storage:** decided 25 Oct 2026. Uploads go on the WROVER-E's SD card (F12), and the built-ins stay in flash.
2. **Idle keep-alive (F07):** should the board send near-silence to the speaker so it never sleeps? Default is no (the speaker's own auto power-off is respected, and the board reconnects on the next tap).
3. **Repeat gap (F04):** default is a seamless loop; a pause of 1 to 5 s between repeats would need a setting. Default is none.
4. **Default Wi-Fi password (F05):** the first boot password stays `gobble123`, printed in the firmware README, and the page nags until it is changed. Say if you want a per-board password instead.
