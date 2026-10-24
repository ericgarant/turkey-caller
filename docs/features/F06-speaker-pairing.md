# F06 Speaker forget and reconnect

**Story:** As a hunter, I pick my speaker once and the board keeps reconnecting to it by itself. I can also forget it and pick another.

## Behavior

- **Search:** `POST /api/bt/scan` runs one inquiry round (about 25 s today, kept; the page may be slow meanwhile). `GET /api/bt/devices` returns `{scanning, devices:[{name, addr, rssi}]}`. Searching stops playback and runs only on request.
- **Use:** `POST /api/bt/connect {addr, name}` saves the speaker **address** (and name for display), connects without restarting if it can; if a restart is needed the page shows "Restarting" and reconnects. A speaker with an empty name is selectable by address.
- **Forget:** `POST /api/bt/forget` disconnects, clears the saved speaker, goes to `idle`, and stops playback. A second search can start immediately.
- **Auto-reconnect:** with a saved speaker the board keeps trying: at boot, after the speaker is switched off and on, and after it comes back in range. Retry cadence backs off (every 3 s for the first minute, then every 10 s) so Wi-Fi stays responsive. No tap is needed and nothing plays by itself on reconnect.
- **No speaker:** play returns 409 with "No speaker connected"; the page disables calls and says why. Volume, settings and upload still work.
- A stale saved speaker (switched off for days) never blocks the page or the Wi-Fi.

## Acceptance criteria

1. Pair the Level Box Slim from a fresh board; status goes searching, connecting, connected, and the header shows the speaker (B2).
2. Switch the speaker off and on, walk out of range and back, reboot the board: back within 30 s each time, then a call plays (B3).
3. Forget, then tap a call: refused with the message; re-pair works; saved volume is untouched (B8).
4. Forget while a call plays: audio stops, no reboot, page shows `idle`.
5. Search with a saved speaker that is off does not break the saved speaker; cancel is not needed.
6. During reconnect attempts the page stays reachable (status answers within 1 s) and `minHeap` stays at or above 40 KB.
