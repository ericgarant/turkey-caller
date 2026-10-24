# F10 Memory and stability floor

A requirement on every feature, because Wi-Fi, Bluetooth and the web server share about 100 KB of free heap and a failed allocation resets the board.

## Rules

1. **Free heap stays at least 40 KB (40960 bytes)** in steady state, during page loads once our handlers run, and during play, under every test in `docs/qa-test-plan.md`, including two phones connected, playing and a speaker search. Outside the transient events below this is `minHeap` in `/api/status`; around them it is the `low=` value on the serial log lines (the lowest free heap since the previous `low=` line).
2. `minHeap` is a low-water mark since boot (never reset by a feature) and `underruns` a running count reset only at boot, both in `/api/status`.
3. Each feature PR states `minHeap` before and after, measured on the board under the stress test (B9), in its description.
4. Large operations stream: uploads write chunk by chunk, JSON is not built from big strings, and no buffer over 4 KB is allocated per request.
5. When heap is low, requests are refused early with 503 and the page retries, instead of letting an allocation fail (already in place from Phase 0; keep it).
6. One heavy action at a time: while a Bluetooth search or an upload runs, the others are refused with 409.
7. No reboot in a 2 hour soak (B16) and none in 50 rapid taps (B9); a reboot is a failure, and the serial log with the reset reason goes in the issue.

## Transient events

Two events are driven by the other device rather than our code, and get their own rule instead of the 40 KB floor: no crash or reset, no audio drop if a clip is playing, and the largest free block stays at least 16 KB. Each has its own row in the measurements on issue #8. Eric approved this rule on 24 Oct 2026, for now; it is up for review later.

- **Speaker reconnect:** from "speaker Connecting" until the stream has started (at most 5 s). Page loads and API calls other than play and stop get 503 meanwhile, and status pushes pause.
- **Phone joining the Wi-Fi:** the second or so after the phone gets its address, when it fires its first DNS lookups, connectivity checks and connections. Test it with a second phone joining while a call repeats.

## Acceptance

- B9 and B16 pass with free heap at or above 40960 outside the transient events (rule 1), the transient rule met inside them, and `underruns` unchanged during the run.
- The architect or reviewer rejects a PR that lowers the heap below the floor, whatever else it adds.
