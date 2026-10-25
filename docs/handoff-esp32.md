# ESP32 coder handoff (25 Oct 2026)

Notes for whoever picks up the firmware next. These are the things the specs and code don't already say.

## Where things stand

| Feature | State |
|---|---|
| F01 built-in page, F02 sound library | Merged (#9, #10) |
| F04 Repeat, F08 volume | Merged (#11). Repeat is one switch on the board, shared by every phone (Eric wants every UI change mirrored on the other phone) |
| F10 heap floor | Merged work in #10 and #11. One open item, tracked in **issue #8** (see below) |
| F06 reconnect, F07 idle wake | PR #14, one state machine in `bt.cpp` |
| F05 Settings | Next. The web thread is building the page against a mock contract: `rev` conflict check, `cfg` in status, a WS `{"evt":"restart"}` before a settings restart. Build the firmware from the contract written in `docs/features/F05-settings.md` / `web/README.md` |
| F03 upload, F09 storage | Deferred by Eric. Keep the 501 stubs; the page hides the controls (`EDIT_SOUNDS=false`) |
| F11 Cooboom speaker | When it arrives |

## Open: issue #8 (F10)

- Taps from the second phone, while two phones are joined, dip free heap to 36.5–39.6 KB for a few seconds. The block stays at 24.5 KB, with no underruns and no resets. The floor is 40 KB.
- Next step: a time-boxed IDF heap trace of one tap with two phones. Keep it as a local patch on Eric's Mac, not in a PR. Seed: `heap_trace_start(HEAP_TRACE_LEAKS)` at the tap, frozen at the low by a sampler task, then decode with addr2line.
- Cheap levers if it turns out to be lwIP/TCP:
  - `CONFIG_LWIP_MAX_ACTIVE_TCP` around 8
  - a smaller listen backlog
  - captive DNS answering A queries only
- The architect asked not to keep tuning the Wi-Fi RX/TX buffer counts.
- **Transient-event rule** (Eric signed off on 24 Oct, in F10): a speaker reconnect and a phone joining don't fall under the 40 KB floor. Instead they must not crash, must not drop audio, and the largest block must stay ≥ 16 KB.

## Measuring heap

- Every serial line that changes memory prints `heap=` (free now) and `low=`. `low=` is the lowest free heap since the previous `low=` line, from `heapmon`. `min=` and `minHeap` in status are the boot-wide minimum.
- When filling a table, take the minimum of `low=` across all lines in the event window, not just the 5 s health lines.
- Things that once cost heap and are now fixed. Don't undo them:
  - `file.setBufferSize(128)` on every open file. The default is a 4 KB stdio buffer.
  - The `/favicon.ico` 204. Without it the redirect fetched the page twice.
  - The request cap (`kMaxRequests` 4, 503 + page retry).
  - The WS queue cap (`WS_MAX_QUEUED_MESSAGES=4`) and skipping a client whose queue hasn't drained.
  - Radio preference: Wi-Fi except while a call plays (+2 s). BALANCE when a phone joined used to cost ~30 KB.
  - The busy window, which serves only play/stop for ≤ 5 s after a speaker connects.

## Build, flash and serial on Eric's Mac

- Eric's Mac runs Claude Code through Remote Control. Start it from this thread with `start_remote_control`. Eric has given a standing OK to flash: flash when ready, and tell him when the Wi-Fi drops and when to test.
- The flash steps are in `firmware/README.md`:
  - `pio run -t upload` for the firmware.
  - `pio run -t uploadfs` only when sounds change.
  - Upload runs at 460800 baud, because the CH340 fails at 921600.
- **Opening the serial port resets the board.** The Remote Control session keeps one continuous timestamped capture per build, for example `/tmp/tc_serial_<sha>.log`, and doesn't reopen the port mid-test.
- Sounds are not openly licensed. **Never commit them.** Only `firmware/data/sounds/index.json` is in git.
- In the cloud sandbox, `pio run -c <ini>` builds with the same `custom_sdkconfig`. Add `-DWS_MAX_QUEUED_MESSAGES=4` if the ini doesn't inherit `build_flags`. Sandbox binaries are larger than the Mac's (1.76 MB vs 1.46 MB), so judge size on the Mac.

## Gotchas

- `millis() | 1` is fine for "0 means unset" flags. As a timestamp compared with a later `millis()` it can be 1 ms in the future, so `now - t` wraps. That caused the radio-preference flapping in #11.
- `server.on()` matches sub-paths, and the first match wins. Register `/api/sounds/delete` before `/api/sounds`.
- Each HTTP request has a single `onDisconnect` slot, and the in-flight counter uses it. Don't set it elsewhere.
- The ESP32-A2DP library delays 10 s in its stack-up handler. That's why the first connect after boot takes about 11 s.
- The library ignores heartbeats while it is "discovering". `bt.cpp` puts it back to "unconnected" after a fruitless search. Without that, reconnect would stop for good.
- Anything that changes the library's state runs on its own task through `bt_app_work_dispatch` (`Source::use`, `forgetSpeaker`, `resumeReconnect`), never from the web or loop task.
- Never allocate (`String`, `malloc`) inside `portENTER_CRITICAL`.
- When editing with scripted search/replace, assert that each pattern matched. A silent miss left `stream`/`error` out of status in #14, and the reviewer caught it.
- Some speakers report "audio started" late or not at all after a connect. `bt.cpp` asks for the stream every 3 s, up to 5 times.

## People and process

- The architect merges. The reviewer reviews every PR.
- Each firmware PR states heap before and after (F10 rule 3).
- Eric tests with a tablet and an Android phone. Give him one combined test list per feature, not one after every fix.
- Commits and PRs carry the attribution lines from the session's instructions. Don't put model names in commits.
