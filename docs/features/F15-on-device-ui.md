# F15 Screen and buttons on the box

**Story:** In the blind, I pick a call and play it with the buttons on the box, reading the call's name on its small screen. I don't need my phone for a hunt. The phone still works at the same time, for uploads, settings, or as a second remote.

**Decided:** Eric picked option A on 31 Oct 2026: the LCD1602 screen and four big push buttons, all from his Freenove kit, so there is nothing to buy. The proposal with the other options (joystick, keypad) is the Claude Doc "On-device display and controls: proposal".

**Depends on:** F12 (WROVER-E), F14 (the three animals). The spare WROOM board gets no screen.

## Hardware (from the Freenove kit)

| Part | Signal | GPIO | Notes |
|---|---|---|---|
| LCD Module 1602, I2C backpack | SDA | 21 | Default I2C pins, as in the Freenove LCD tutorial. 100 kHz, the PCF8574 backpack's rated speed |
| | SCL | 22 | |
| | VCC | 5V | The **5V** rail, not the 3.3 V one: Eric's module (GJD 1602IIC) has no contrast screw, and at 3.3 V it lights blue with no visible text (31 Oct 2026). Modules with a blue contrast pot can be adjusted with it |
| | GND | GND | |
| Big push button, yellow cap | Up | 32 | Between the pin and GND. Internal pull-up, no resistor |
| Big push button, blue cap | Down | 33 | Same (GPIO 33 was the F13 LED, now free) |
| Big push button, green cap | Play | 25 | Same |
| Big push button, red cap | Stop | 26 | Same. Put it apart from the others on the box, so it's found by feel |

- The backpack's I2C address is 0x27 (PCF8574T chip) or 0x3F (PCF8574AT). Eric's module is **0x3F** (probe, 31 Oct 2026); the firmware still tries both at boot.
- **Button legs:** each button has 4 legs. The two legs facing each other across the breadboard's middle groove are always connected; only the top and bottom legs on the same side are switched. Wire the pin to one and GND to the other, on the same side. Wired across the groove, the pin reads pressed all the time (that was the first probe run).
- Untouched, per the architecture rules: the SD card pins (2, 14, 15, and 4 and 13), the PSRAM (16, 17), the flash (6 to 11), GPIO 12 and GPIO 0 (the BOOT button, already the 5 s reset).
- Still free afterwards: 5, 18, 19, 23, 27, and 34 and 35 (input only).
- The backpack's pull-ups sit on its 5 V supply. This is how the Freenove tutorial wires it, with the ESP32's own I2C pins, and the probe confirms it works on this board.

## Step 1: probe (`firmware/tools/ui-probe`)

On the breadboard, before any firmware change. `pio run -d firmware/tools/ui-probe -t upload -t monitor`. The probe:

- scans I2C and prints the address it finds (or "no LCD found");
- writes "Turkey caller" and "probe" on the LCD, then shows each press on the second line ("Green press 3", "Red hold 1.2 s");
- prints each press and hold on serial, with the time it took to rewrite the 32 characters;
- turns the backlight off after 30 s without a press, and the next press wakes it.

Pass if the text is readable on the LCD (not only the address on serial), the address is printed and recorded in `firmware/README.md`, every button shows one press per push (no doubles in 20 quick pushes each), a hold shows as a hold, and the backlight turns off and wakes.

## Screens and controls (step 2)

Two screens, 16 x 2 characters.

**Picking a call** (shown at boot, after a 2 s "Turkey caller" and version):

```
Moose       4/13
Cow moan
```

- Line 1: the animal on the left, the position in that animal's list on the right. Line 2: the call's name. A name over 16 characters scrolls, then pauses at the start.
- The lists are the page's tabs: the same animals, calls and order as `GET /api/sounds` (turkey calls include "Your recordings"). An animal with no calls is skipped.
- **Up / Down:** previous or next call; it wraps around.
- **Red:** next animal (Turkey, Moose, Deer, then Turkey again), at its first call.
- **Green:** play the call shown, with the shared Repeat switch, exactly like a tap on the page. The screen changes to Playing.
- The box starts at the first turkey call after each restart (nothing is saved).

**Playing:**

```
Playing 0:12   R
Cow moan
```

- Line 1: the time played, and `R` when Repeat is on. Line 2: the call's name.
- **Up / Down:** volume up or down by 5 (0 to 100). Line 1 shows "Volume 60" for 1.5 s.
- **Hold green (1 s):** Repeat on or off. Line 1 shows "Repeat on" or "Repeat off" for 1.5 s. A short green press does nothing here.
- **Red:** stop. The screen goes back to Picking, on the same call.
- When the call ends, the screen goes back to Picking, on the same call.

**Shared with the phones.** The box and the phones drive the same player, volume and Repeat switch. A call started on a phone shows on the box (Picking moves to that call, switching animal if needed), and one started on the box shows on the phones. Volume and Repeat changes on the box are pushed to the phones like any other change.

**Speaker and errors.** Green with the speaker asleep or disconnected wakes or reconnects it the same way a tap on the page does (F06, F07): line 1 shows "Speaker..." until the call starts. If the speaker doesn't answer within the page's 8 s, or a call can't play (card gone, busy), line 1 shows a short reason for 3 s ("No speaker", "No SD card", "Busy, try again") and the screen stays on Picking. No separate reconnect button is needed.

**Backlight.** Off after 30 s without a press, so the box doesn't light up the blind. While it's off, the first press of any button only turns it back on and does nothing else. A call started from a phone does not turn it on.

**No screen.** If no LCD answers at boot, serial says `LCD: none` once and the caller works as before. The buttons still work, without the screen.

## Firmware rules

- New module `ui`, built only with `TC_HAS_UI` (set for `wrover`, not for `wroom`, like `TC_HAS_SD`). It calls the same play, stop, volume and Repeat code as `web`; it never touches the player or Bluetooth state directly. The shared Repeat switch moves out of `web.cpp` so both modules set the same one.
- Buttons: 30 ms debounce, a hold after 1 s. A press is never lost while the screen is being rewritten: reading the buttons must not wait on the LCD.
- A full 2-line rewrite took 59 ms at 100 kHz on the probe. The firmware stays at 100 kHz (the PCF8574 is rated for it) and writes only the characters that changed (a timer tick is one or two), so most updates take a few ms. The buttons are sampled by a 5 ms timer, apart from the screen.
- The screen is written only by the `ui` task, only after a press or a state change (at most every 250 ms while playing for the timer). The task runs at a lower priority than the player's decode task (priority 5 on core 1), so pressing buttons can't cause a dropout.
- The task's stack is 4 KB (3 KB left only 1.2 KB free while idle on the board), from internal RAM. The LCD library is LiquidCrystal_I2C (the one the Freenove tutorial uses), pinned to an exact version.
- F10 and the 2.25 MB budget still apply: the PR reports `minHeap` and the app size against main.
- No change to the API, the mock or the page, except that a call started from the box appears on the page like one started from another phone.

## Acceptance criteria (new QA row B21)

1. The probe's I2C address and the pins are in `firmware/README.md`.
2. Every call in all three animals plays from the buttons, with 0 underruns.
3. Pressing Up and Down fast for 30 s while a call plays causes no dropout, and the internal heap stays over 40 KB.
4. A call started on the box shows on both phones, and one started on a phone shows on the box. Volume and Repeat changed on either side show on the other.
5. Red stops from the Playing screen. With the speaker off, green shows "Speaker...", then plays once the speaker is on, or shows "No speaker".
6. The backlight turns off after 30 s, and the first press only wakes it.
7. With the LCD unplugged, the box boots, the phones work, and the fast pass (B1, B2, B4, B5, B9) still passes.

## Phone-free mode (step 3)

Eric asked for it on 31 Oct 2026, after the box passed B21.

- **Switch:** on the Picking screen, hold **red 3 s**. The box shows "Wi-Fi off" / "Restarting...", saves the choice and restarts with no access point and no web server. Hold red 3 s again to turn Wi-Fi back on (same restart).
- **Red on Picking now acts on release:** a short press switches animal, a 1 to 3 s press does nothing, and 3 s switches Wi-Fi. On the Playing screen red still stops at once, on the press, and that push never also switches animal.
- **The choice lasts across restarts and power cycles.** The boot screen shows "Wi-Fi off" instead of the version while it is on.
- **Way back without the box:** with Wi-Fi off, the BOOT button held 5 s turns Wi-Fi back on and restarts. In this mode it does only that: the Wi-Fi name, password and device name stay (with Wi-Fi on, BOOT 5 s is still the factory reset).
- Bluetooth, the speaker, volume and Repeat work as before. The page's Instructions explain the mode.

### Acceptance (QA row B22)

1. Hold red 3 s on Picking: the board restarts with no `TurkeyCaller` network. Calls play from the box with 0 underruns.
2. The internal heap with Wi-Fi off is reported against Wi-Fi on (`heap after ...` lines and `low=`).
3. Power off and on: Wi-Fi stays off. Hold red 3 s: Wi-Fi comes back and the phone joins.
4. With Wi-Fi off, BOOT held 5 s brings Wi-Fi back.
5. A short red press on Picking still switches animal, and red during a call stops it without switching animal.

## Later (not in this spec)

- **No Bluetooth speaker:** the kit's Audio Converter & Amplifier and its small speaker could play calls at short range.
