# F05 Settings: Wi-Fi name, password and device name

**Story:** As the owner, I give my board its own Wi-Fi name and password so strangers and the next hunter's board do not clash.

## Behavior

- `GET /api/config` returns `{"ap_ssid","device_name","default_pass":bool,"rev":N}`, never the password. `POST /api/config` with `ap_ssid`, `device_name`, optional `ap_pass` (empty means unchanged) and `rev` (the rev the page loaded). Reply `{"ok":true,"restart":bool}`.
- Rules: Wi-Fi name 1 to 32 characters; password 8 to 63 characters (WPA2), or unchanged; device name 1 to 24 characters. Limits are counted in UTF-8 bytes, so an accented letter counts as 2; the page checks the same way. Invalid values are refused with 400 and a clear error, and nothing is saved.
- **Live on every phone** (Eric, 24 Oct 2026: every setting syncs across devices). `rev` is a RAM counter seeded with `esp_random()` at boot (31 bits, never 0) and +1 on every saved change; it is also in `/api/status` as `"cfg":rev` and part of the "shown" state, so a change is pushed at once. A page that sees a new `cfg` reloads `/api/config`, keeps fields the user is editing and takes the rest. A POST whose `rev` doesn't match is refused with 409 `changed on another phone` and nothing is saved, so two phones never silently overwrite each other. A POST without `rev` is accepted (scripts, older pages).
- A device-name-only change is saved to NVS without a restart (`restart:false`); both phones update at once and the Bluetooth name applies at the next restart. A Wi-Fi name or password change is saved, the board replies `restart:true`, sends `{"evt":"restart","reason":"settings","ssid":"<new name>"}` to every socket (`ws.textAll`), and restarts about 1 s later. The page shows "Saved. Rejoin <name>" (the other phone: "changed on the other phone") and reconnects on its own when the board returns. The `evt` message never carries the password; status messages never have an `evt` field.
- The device name shows in the page header, and is used as the Bluetooth device name the speaker sees (applies after restart).
- First boot defaults: name `TurkeyCaller`, password `gobble123`, device name `Turkey Caller`. Until the password is changed, the Settings screen shows a one-line reminder.
- Recovery: if the password is lost or forgotten, hold the BOOT button (GPIO0) for 5 s **while the board is running**. The firmware reads GPIO0 as a normal input after boot; it cannot be done at power-up, because BOOT held low at reset puts the ESP32 in the ROM download mode and the firmware never runs. After 5 s the board prints a line on serial, resets Wi-Fi name, password and device name to the defaults (it keeps the speaker, volume and sounds) and restarts. Just before the restart it sends `{"evt":"restart","reason":"reset","ssid":"TurkeyCaller"}`, and phones still connected show "Wi-Fi settings reset".
- Keeping the same channel and max 2 clients. A setting change never changes the saved speaker.

## Acceptance criteria

1. Change name and password; board restarts; phone rejoins with the new password; old password no longer works (B14).
2. A 7-character password is refused; so are an empty name and a 33-character name.
3. After power cycles the new name and password persist; no filesystem or NVS error on serial (B10).
4. With the board running, holding BOOT for 5 s restores the defaults and keeps sounds, speaker and volume; a tap shorter than 5 s does nothing; holding BOOT at power-up enters download mode and is not part of this feature.
5. Posting the same values (or only a device name change) works; a device-name-only change need not drop the Wi-Fi link beyond the restart.
6. Password never appears in `/api/*` replies, WebSocket messages, the serial log, or the page source.
7. Two phones on Settings: a device name saved on one shows on the other within 1 s with no restart; a Wi-Fi change shows the rejoin message on both; a save from a phone holding an old `rev` gets 409 and changes nothing (`web/test/settings.mjs` on the mock, B14 on the board).
