# F09 More room for sounds

**Problem:** The 1.375 MB LittleFS partition is about 91% full with the 12 calls, which leaves almost nothing for phone uploads (F03). The app uses about 1.75 MB of its 2.5 MB partition and the build budget is 2.25 MB.

## Requirement

The user can upload at least **8 more clips of 15 s each** (about 1.3 MB at 11 KB/s) after the 12 built-in calls are loaded, with at least 32 KB left free (LittleFS needs headroom for its own work). Room is reported correctly in the page's storage meter.

## Options (architect decides, in this order)

1. **Measure first.** With the final page and API linked, report the real app size. The flash is 4 MB: the app partition can be sized to the real binary plus a margin (about 10%) instead of 2.5 MB.
2. **Rebalance the partitions.** Free flash is `4 MB − 64 KB coredump − NVS/phy − app − LittleFS`. At a 2.0 MB app, LittleFS can be about 1.87 MB; at 2.25 MB it can be about 1.62 MB. Dropping the 64 KB coredump partition gives a bit more. There is no OTA, so no second app slot is needed. Changing the layout needs one full flash of both app and `uploadfs`; document it in the firmware README.
3. **Smaller sounds.** Trim silence in the built-in clips, and cap uploads at 60 s (F03). Going below 22.05 kHz is not wanted (audible loss on yelps).
4. **Shrink the app.** Prune libraries and the page; this is the last resort because Phase 0 already trimmed the sdkconfig.

Not in scope: a microSD card or external flash (the board has none), and MP3 on the board (the heap cost was ruled out in the architecture).

## Acceptance criteria

1. `check_size.py` budget is updated to match the new layout (still 90% of the app partition) and the build fails when it is passed.
2. `/api/status` `fsFree` and `fsTotal` are right after boot, after each upload and after each delete; `fsFree` is recomputed after writes, never cached stale.
3. With the 12 calls loaded, a test uploads 8 clips of about 15 s and all are playable; the 9th upload that does not fit gets 413 and leaves the others intact (B13).
4. After a full pass of uploads and deletes, no filesystem error on serial and free space returns after deleting.
5. The README states the layout and the flash order after a layout change.
