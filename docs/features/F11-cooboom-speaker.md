# F11 Future Cooboom speaker

**Story:** As the owner, when I replace the Level Box Slim with the Cooboom 20 W speaker, I switch speakers from the page and everything works as before.

**Status:** future. The speaker is not here yet, so only the speaker-independent parts are built now; the rest is a checklist to run when it arrives.

## Build now (no hardware needed)

- No speaker-specific code: nothing in the firmware or page names a model. The saved speaker is identified by Bluetooth address, with the name shown for display only (F06).
- Pairing works with PIN-less (SSP) speakers, and also with legacy PIN 0000 where a speaker asks for one.
- Sample rate and codec are fixed (SBC, 44.1 kHz stereo from mono); a speaker that offers only another rate is reported in the serial log and the page says "This speaker is not supported" rather than playing at the wrong speed.

## Checklist when the Cooboom arrives

1. Run the full board pass (B2 to B12, then B17) with the Cooboom as the speaker; record results in the PR or issue for that firmware version.
2. Check things that differ between speakers: its own volume (the board's 100 should not be too loud or distorted on 20 W; set a safer default if needed), power-off timeout and wake time (F07), connecting to a previously paired phone first (the board must still win the connection once the phone's Bluetooth is off), and loud or soft turn-on beep.
3. Forget the Level Box and pair the Cooboom with no reboot of the board needed by hand, and the other way round.
4. Record any speaker-specific quirk in `firmware/README.md` under a "Speakers tested" table (speaker, result, quirks).

## Acceptance criteria

1. Everything in B2 to B12 and B17 passes on the Cooboom with no code change specific to it, or any needed change is behind a generic setting and documented.
2. Switching the Level Box to the Cooboom and back takes under 2 minutes from the page.
3. Heap floor (F10) holds with the Cooboom.
