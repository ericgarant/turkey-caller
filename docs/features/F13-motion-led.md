# F13 Motion sensor and LED

**Story:** I leave the caller near a decoy. When something moves in front of it, the LED on the board lights, so I can see it from my blind without looking at my phone. Later, I may want motion to play a call I choose.

**Hardware (WROVER-E board, added by Eric on 25 Oct 2026):**
- An HC-SR501 PIR motion sensor. Its output is on GPIO 13.
- An LED on GPIO 33.

The HC-SR501 has its own time and sensitivity knobs and a jumper for single or repeat trigger. Its output stays high for the time set on the knob, 3 s at minimum.

**Pin check first.** On some WROVER boards with an SD slot, GPIO 13 is also the card's D3 line. If the PIR holds it low while the card starts, the SD card may not mount (F12). The F12 SD probe checks this. If they clash, the PIR moves to a free input-only pin (GPIO 34, 35, 36 or 39; the HC-SR501 drives its output itself, so it needs no pull-up). Whatever pin it ends up on goes in `firmware/README.md`. GPIO 33 is fine for the LED.

## Step 1 (build now, after F12)

- **LED on motion:** the LED is on while the sensor output is high, plus 2 s after, so a short trigger is still visible. Otherwise it's off.
- **Motion in status:** `/api/status` adds `motion` (true or false) and `motionAt` (seconds since boot of the last trigger, 0 if none). `motion` is part of the pushed state, so both phones get it at once. This costs a few bytes and lets the page show it later. The page itself doesn't change in step 1.
- **Setting:** an "LED on motion" switch in Settings, on by default, saved in NVS (F05 style, synced on both phones). This way the LED can be turned off where a light would give the hunter away.
- **Warm-up:** the HC-SR501 gives false triggers for about 60 s after power-up. The firmware ignores the sensor for the first 60 s after boot.
- Reading the sensor is a cheap pin read in the main loop or a pin interrupt. No new task, and no effect on the audio path or the heap floor (F10).
- A serial line on each new trigger: `motion at <uptime>`.

## Step 2 (later, only when Eric asks)

Motion plays a call the user picks in Settings. Default is off. The design to start from:
- the sound is chosen from the list
- a cooldown (default 5 min), so one turkey strutting doesn't trigger it ten times
- it never replaces a call that is already playing
- it follows the same speaker rules as a tap (wake, 409 with no speaker)

The status fields from step 1 are enough to build on.

## Acceptance criteria (step 1, QA row B19)

1. The PIR's final pin is recorded, and the SD card still mounts with the PIR connected (F12 B18 still passes).
2. Wave a hand in front of the sensor after the 60 s warm-up: the LED lights within 0.5 s, stays on while the sensor is high plus about 2 s, then goes off.
3. No LED or `motion` change during the first 60 s after boot.
4. With "LED on motion" off, the LED stays dark on motion, but `motion` still changes in status. The setting survives a restart and shows on both phones.
5. Motion during playback doesn't affect the audio: no underruns, and the heap floor holds (F10).
