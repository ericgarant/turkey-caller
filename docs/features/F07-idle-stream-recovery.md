# F07 Idle speaker stream recovery

**Problem:** After minutes of nothing playing, many speakers suspend the Bluetooth audio stream, sleep, or drop the link. The next tap can then be silent, cut off at the start, or late.

## Behavior

- The board knows three link states for the speaker: connected and streaming ready, connected but suspended (A2DP suspend or no media started), and disconnected. `/api/status` exposes this (`bt` stays `connected` or `connecting`; add `stream`: `ready`, `waking`, `suspended` or `down`, and an `error` text after a failed wake).
- **Play after idle:** on a tap, if the stream is not ready, the board starts it (A2DP media start), or reconnects if the link is down, then plays. The page shows "Waking the speaker" while this happens.
- **Pre-roll:** the first audible sample is preceded by short silence (about 250 ms) only when the stream had to be woken, so the speaker's amp has time to open and the call is not clipped at the start. It is not added when the stream is already ready.
- **`/api/play` never waits:** the web handler runs on the AsyncTCP task and must not block, and an open request holds a socket and heap (F10). `POST /api/play` answers at once: 200 when the stream is ready and the sound starts now, 202 `{"waking":true}` when the stream has to be woken first, 409 when no speaker is saved or connecting is not possible. The outcome arrives through `/api/status` and `WS /ws` as the `stream` field and `playing`.
- **Give up cleanly:** if the speaker does not come back within 8 s, the board drops the pending play, sets an error in status ("Speaker did not answer") that the page shows as a message, and background reconnect continues (F06). A tap never hangs and never crashes the board.
- **Keep-alive:** off by default. The board does not stream silence to hold the speaker awake, so the speaker's own power saving works. (Open decision for Eric; if turned on later it is a Settings switch.)
- No audio comes out by itself at any point of recovery (no stale buffered sound after waking).

## Acceptance criteria (new QA row B17, idle recovery)

1. Leave the board connected and idle for 5, 15 and 60 minutes; tap a call each time. Each time the call is audible from its first syllable within 3 s of the tap, or the page says why.
2. Switch the speaker off during idle, tap a call: the page says "Speaker did not answer" within 8 s, then after switching the speaker on, the board reconnects by itself and the next tap plays.
3. With the speaker out of range for 10 minutes then back, reconnect happens within 30 s and a tap plays.
4. No stale sound plays after waking; serial log shows the stream state changes.
5. Heap stays at or above 40 KB through all of the above, and underruns do not rise from the wake-up.
