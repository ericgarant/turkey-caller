# F13 Motion sensor and LED

**Story:** I leave the caller near a decoy. When something moves in front of it, the LED on the board lights, so I can see it from my blind without looking at my phone. Later, I may want motion to play a call I choose.

**Hardware (WROVER-E board, added by Eric on 25 Oct 2026):**
- An HC-SR501 PIR motion sensor, soldered to GPIO 13. Power it from **5 V (VIN)**, not 3.3 V: it has its own 3.3 V regulator and is unreliable when fed 3.3 V. Its output is 3.3 V logic, so it connects straight to the ESP32.
- An LED on GPIO 33, **with a series resistor** of about 330 Ω to 1 kΩ.

**Sensor setup:** set the jumper to **H (repeat trigger)** and turn the time knob **fully counter-clockwise** (about 3 s). Continuous movement then keeps the output high, and the firmware owns how long the LED stays on. After its output drops, the module ignores motion for about 2.5 to 3 s (its block time). The sensitivity knob is Eric's call. Mount the sensor a few cm away from the ESP32 module's antenna, or on a short cable: its amplifier picks up 2.4 GHz and false-triggers when it sits right next to Wi-Fi and Bluetooth.

**Sensor pin: GPIO 13 (soldered; it can't move).** On SD-slot boards, GPIO 13 is often the card's DAT3 line. A card that sees DAT3 low when it starts switches to SPI mode, and the SD mount fails. The HC-SR501's output idles low. The card samples DAT3 only at startup, and 1-bit mode ignores it afterwards. So:
- The F12 probe checks whether GPIO 13 goes to the slot's DAT3, and tries a mount with the sensor idle and again with it high (someone waving at it).
- If only the high case mounts, the firmware retries the SD mount at boot for up to about 60 s. During that time, serial and the page say "Wave at the motion sensor to start the SD card". If no mount happens by then, the card is treated as missing (F12 no-card behavior) until the next restart.
- GPIO 13 is only ever an input, with no internal pull-down that would fight the sensor or the board's pull-up.

If a sensor is ever wired fresh, use GPIO 34 or 35 instead (input-only). Avoid 36 and 39 (ESP32 errata 3.11). GPIO 33 is fine for the LED.

## Step 1 (build now, after F12)

- **LED on motion:** the LED is on while the sensor output is high, plus 2 s after, so a short trigger is still visible. Otherwise it's off.
- **Motion in status:** `/api/status` adds `motion` (true or false) and `motionAt` (seconds since boot of the last trigger, 0 if none). `motion` is part of the pushed state (`Shown`), so both phones get it at once. With the sensor's ~3 s minimum cycle, that's at most about one push every 3 s. `motionAt` is not in `Shown`, so it doesn't cause extra pushes.
- **Setting (firmware and web):** an "LED on motion" switch in Settings, on by default, so the LED can be turned off where a light would give the hunter away. This extends the F05 contract:
  - `led_motion` (`1` or `0`) in `GET` and `POST /api/config`
  - a change saves to NVS and bumps `rev`, with no restart
  - it syncs to both phones like the device name
  - the mock and `web/test/api.mjs` / `settings.mjs` cover it

  The page's only change in step 1 is that switch. It doesn't show motion yet.
- **Warm-up:** the HC-SR501 gives false triggers for 30 to 60 s after it's powered. The firmware ignores the sensor for 60 s after a power-on boot (`esp_reset_reason() == ESP_RST_POWERON`). A restart from a settings change or the BOOT reset doesn't cut the sensor's power, so it skips the wait.
- Reading the sensor is a cheap pin read in the main loop or a pin interrupt. No new task, and no effect on the audio path or the heap floor (F10).
- A serial line on each new trigger: `motion at <uptime>`.

## Step 2 (later, only when Eric asks)

Motion plays a call the user picks in Settings. Default is off. The design to start from:
- the sound is chosen from the list
- a cooldown (default 5 min), so one turkey strutting doesn't trigger it ten times (the sensor's own ~3 s block time is far too short for that)
- it never replaces a call that is already playing
- it follows the same speaker rules as a tap (wake, 409 with no speaker)

The status fields from step 1 are enough to build on.

## Acceptance criteria (step 1, QA row B19)

1. With the PIR on GPIO 13, the SD card mounts at boot, waving at the sensor if the probe showed that's needed, and F12 B18 still passes. The pins, whether the wave is needed, and the sensor's power source are in `firmware/README.md`.
2. After the warm-up, wave a hand in front of the sensor: the LED lights within 0.5 s, stays on while there is movement plus about 2 s, then goes off.
3. No false triggers: 5 minutes with nothing moving in front of the sensor while a call repeats (Wi-Fi and Bluetooth busy) gives no `motion at` line.
4. After a power-on, no LED or `motion` change for the first 60 s. After a settings restart, the sensor works right away.
5. With "LED on motion" off, the LED stays dark on motion, but `motion` still changes in status. The setting survives a restart and shows on both phones. A stale save gets 409, as in F05.
6. Motion during playback doesn't affect the audio: no underruns, and the heap floor holds (F10).
