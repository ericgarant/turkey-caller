# F08 Volume persistence

**Story:** As a hunter, I set the volume once and it is the same after the battery swap.

## Behavior

- Volume is 0 to 100, applied by software gain on the board (the speaker's own volume is not touched). First boot default is 70 (what the merged firmware uses; the mock in `web/mock/server.mjs` starts at 60 and changes to 70 in the same PR as the next firmware change). Out-of-range input is clamped, not refused.
- The level is saved to NVS about 2 s after the last change, not on every change, so dragging the slider does not wear flash. The write happens at a quiet moment: not in the audio path, never while a flash write could stall the decoder.
- On boot the saved level is applied before the first sound can play.
- The page, the WebSocket `volume` command, and `POST /api/volume` all end in the same saved state; every connected page shows the new level within 1 s.
- Volume 0 is silent. Volume steps never produce a click or a jump (gain ramps over a few milliseconds).

## Acceptance criteria

1. Set 30, wait 3 s, power cycle: volume is 30 after boot and a call plays at that level (B7, B10).
2. Dragging the slider for 10 s causes one NVS write (check the serial log), not one per move.
3. Power pulled within 1 s of a change may lose that change; it never corrupts the saved value or any other setting.
4. Two phones: moving the slider on one updates the other within 1 s.
5. 100 is not distorted on the Level Box Slim; a call at 0 is silent and recovers when raised.
