# Feature specs: next phase

Specs for what comes after Phase 0 (the Wi-Fi test page and Bluetooth ADPCM playback in PR #3, the control page in PR #4, the QA plan in PR #5). Each spec says what the hunter sees, how it must behave, and the acceptance criteria that QA turns into tests (`docs/qa-test-plan.md`). They do not say how to code it; the ESP32 coder and the web coder choose that, within the constraints in `docs/architecture.md`.

## Specs

| # | Spec | Who builds it | Test rows |
|---|---|---|---|
| F01 | [Firmware serves the built-in page](F01-built-in-page.md) | Firmware | B1, B15 |
| F02 | [Sound format and library](F02-sound-format-library.md) | Firmware, web | B4, B10 |
| F03 | [Phone upload with conversion in the page](F03-phone-upload.md) | **Deferred** | B13 |
| F04 | [Repeat](F04-repeat.md) | Firmware, web | B6 |
| F05 | [Settings: Wi-Fi and device name](F05-settings.md) | Firmware, web | B14, B10 |
| F06 | [Speaker forget and reconnect](F06-speaker-pairing.md) | Firmware, web | B2, B3, B8 |
| F07 | [Idle speaker stream recovery](F07-idle-stream-recovery.md) | Firmware | new B17 |
| F08 | [Volume persistence](F08-volume-persistence.md) | Firmware | B7, B10 |
| F09 | [More room for sounds](F09-storage.md) | **Deferred** (only needed for F03) | B13 |
| F10 | [Memory and stability floor](F10-heap-floor.md) | Everyone (cross-cutting) | B9, B16 |
| F11 | [Future Cooboom speaker](F11-cooboom-speaker.md) | Firmware, QA | B2 to B12 rerun |

## Suggested build order

1. F10 stays in force the whole time. Every PR that touches firmware reports `minHeap` from `/api/status`.
2. F01 and F02 together (real page on the board, WAV names and sizes in the sound list).
3. F08, F04 (small, and the page already has the controls).
4. F06, F07 (the speaker work; do F07 right after F06 so reconnect and recovery share one state machine).
5. F05.
6. F11 when the Cooboom arrives.

## Deferred

Eric decided on 24 Oct 2026 to skip uploading his own recordings for now. The specs stay as written so the work can resume later, but nobody builds them until he asks.

- **F03 Phone upload.** The firmware keeps answering `POST /api/sounds` and `POST /api/sounds/delete` with `501`, and the "Add a sound" controls in Settings should be hidden (small change, in the next web or firmware PR that touches the page) so nobody hits the 501.
- **F09 More room for sounds.** It only existed to make room for uploads. The current layout (2.5 MB app, 1.375 MB LittleFS) holds the 12 built-in calls, so it stays. Trimming the built-ins is still welcome but no longer required.

If F03 comes back, do F09 first (storage decision), then F03.

## Decisions for Eric

Defaults are written into the specs so work is not blocked; change them here and the specs follow.

1. **Storage (F09, deferred with F03):** how to get more room for uploads. 4 MB limits it to about 60 to 97 s of uploads depending on the real size of the built-ins, so the spec asks for at least 60 s, target 90 s. Recommended: keep all 12 calls at 22.05 kHz and spend the first F09 PR on reading the real numbers (the board was reported 91% full; the architecture doc estimates 60 to 65%) before choosing a partition layout.
2. **Idle keep-alive (F07):** should the board send near-silence to the speaker so it never sleeps? Default is no (the speaker's own auto power-off is respected, and the board reconnects on the next tap).
3. **Repeat gap (F04):** default is a seamless loop; a pause of 1 to 5 s between repeats would need a setting. Default is none.
4. **Default Wi-Fi password (F05):** the first boot password stays `gobble123`, printed in the firmware README, and the page nags until it is changed. Say if you want a per-board password instead.
