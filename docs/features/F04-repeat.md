# F04 Repeat

**Story:** As a hunter, I turn Repeat on, tap a call, and it keeps calling until I tap Stop.

## Behavior

- `POST /api/play {id, loop=1}` loops until `/api/stop`, a different play, or the speaker disconnecting. `/api/status` returns `loop` as true while a looping sound plays, and `pos_ms` wraps at each pass.
- The loop is seamless: the next pass starts from the buffer, with no silent gap longer than 50 ms and no click at the join. (A configurable pause between passes is not in this phase.)
- The page's Repeat switch is remembered on the phone and sent with every play.
- Tapping the playing call stops it; tapping another call replaces it, keeping the loop setting.
- Stop works within 0.5 s at any point in a loop. Volume changes apply during a loop.
- If the speaker disconnects, the loop ends and the page shows the speaker state. After the speaker reconnects, nothing plays by itself.

## Acceptance criteria

1. A 5 s call on repeat plays for 2 minutes with no dropout, no gap over 50 ms, underruns unchanged (B6).
2. Stop during the 40th pass silences in under 0.5 s.
3. `minHeap` is unchanged by a 10 minute loop and an hour of repeat shows no drift in `uptime`-normalized underruns.
4. The page shows Repeat state correctly after a reload while a loop is playing (read from `loop` in status).
