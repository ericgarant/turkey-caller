# F10 Memory and stability floor

A requirement on every feature, because Wi-Fi, Bluetooth and the web server share about 100 KB of free heap and a failed allocation resets the board.

## Rules

1. **Lowest heap since boot is at least 40 KB (40960 bytes)**, as `minHeap` in `/api/status`, under every test in `docs/qa-test-plan.md`, including two phones connected, playing, uploading and a speaker search.
2. `minHeap` is a low-water mark since boot (never reset by a feature) and `underruns` a running count reset only at boot, both in `/api/status`.
3. Each feature PR states `minHeap` before and after, measured on the board under the stress test (B9), in its description.
4. Large operations stream: uploads write chunk by chunk, JSON is not built from big strings, and no buffer over 4 KB is allocated per request.
5. When heap is low, requests are refused early with 503 and the page retries, instead of letting an allocation fail (already in place from Phase 0; keep it).
6. One heavy action at a time: while a Bluetooth search or an upload runs, the others are refused with 409.
7. No reboot in a 2 hour soak (B16) and none in 50 rapid taps (B9); a reboot is a failure, and the serial log with the reset reason goes in the issue.

## Acceptance

- B9 and B16 pass with `minHeap` at or above 40960 and `underruns` unchanged during the run.
- The architect or reviewer rejects a PR that lowers `minHeap` below the floor, whatever else it adds.
