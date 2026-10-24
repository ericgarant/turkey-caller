# F09 More room for sounds

**Problem:** The 1.375 MB LittleFS partition was reported about 91% full with the 12 calls, which would leave almost nothing for phone uploads (F03). `docs/architecture.md` estimates the clips at about 0.83 MB (60 to 65%), so the two figures disagree; the first task is to read the real numbers (`fsFree`, `fsTotal`, and `LittleFS: used of total` on the serial log) and put them in the PR. LittleFS overhead and the longer clips may explain the gap. The app uses about 1.75 MB of its 2.5 MB partition and the build budget is 2.25 MB.

## Requirement

Room for uploads is stated in seconds of audio (about 11 KB per second), after the 12 built-in calls are loaded and with at least 32 KB left free for LittleFS:

- **Minimum: 60 s** of uploads (about 4 clips of 15 s). This is met by the fallback layout (2.25 MB app, about 1.62 MB LittleFS) only if the built-ins take about 0.9 MB or less.
- **Target: 90 s** (about 6 clips of 15 s), which needs the tightest layout below.
- The arithmetic: free upload room = LittleFS size − built-ins − 32 KB. At a 1.85 MB app (today's 1.75 MB plus about 5%), LittleFS is at most about 2.02 MB with the coredump partition, or 2.09 MB without it. With the built-ins at 0.9 MB that is about 1.1 MB, or 97 s. If the board reports the built-ins at 1.25 MB (91%), room drops to about 0.74 MB, or 67 s, so trimming the built-ins matters then.
- More than that is not possible in 4 MB without cutting built-ins or lowering the sample rate; the architect decides whether to ask Eric for that.

Room is reported correctly in the page's storage meter.

## Options (architect decides, in this order)

1. **Measure first.** With the final page and API linked, report the real app size. The flash is 4 MB: the app partition can be sized to the real binary plus a margin (about 10%) instead of 2.5 MB.
2. **Rebalance the partitions.** Free flash is `4 MB − 64 KB coredump − NVS/phy − app − LittleFS`. At a 2.0 MB app, LittleFS can be about 1.87 MB; at 2.25 MB it can be about 1.62 MB. Dropping the 64 KB coredump partition gives a bit more. There is no OTA, so no second app slot is needed. Changing the layout needs one full flash of both app and `uploadfs`; document it in the firmware README.
3. **Smaller sounds.** Trim silence in the built-in clips, and cap uploads at 60 s (F03). Going below 22.05 kHz is not wanted (audible loss on yelps).
4. **Shrink the app.** Prune libraries and the page; this is the last resort because Phase 0 already trimmed the sdkconfig.

Not in scope: a microSD card or external flash (the board has none), and MP3 on the board (the heap cost was ruled out in the architecture).

## Acceptance criteria

1. `check_size.py` budget is updated to match the new layout (still 90% of the app partition) and the build fails when it is passed.
2. `/api/status` `fsFree` and `fsTotal` are right after boot, after each upload and after each delete; `fsFree` is recomputed after writes, never cached stale.
3. With the 12 calls loaded, a test uploads clips until the room is gone: at least 60 s in total fit and are playable (target 90 s), and the first upload that does not fit gets 413 and leaves the others intact (B13).
4. After a full pass of uploads and deletes, no filesystem error on serial and free space returns after deleting.
5. The README states the layout and the flash order after a layout change.
