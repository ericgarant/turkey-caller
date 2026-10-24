# F05 Settings: Wi-Fi name, password and device name

**Story:** As the owner, I give my board its own Wi-Fi name and password so strangers and the next hunter's board do not clash.

## Behavior

- `GET /api/config` returns `ap_ssid` and `device_name`, never the password. `POST /api/config` with `ap_ssid`, `device_name`, and optional `ap_pass` (empty means unchanged).
- Rules: Wi-Fi name 1 to 32 characters; password 8 to 63 characters (WPA2), or unchanged; device name 1 to 24 characters. Invalid values are refused with 400 and a clear error, and nothing is saved.
- Saved to NVS, then the board replies `{"ok":true}` and restarts after about 1 s. The page shows "Saved. Rejoin <name>" and reconnects on its own when the board returns.
- The device name shows in the page header, and is used as the Bluetooth device name the speaker sees (applies after restart).
- First boot defaults: name `TurkeyCaller`, password `gobble123`, device name `Turkey Caller`. Until the password is changed, the Settings screen shows a one-line reminder.
- Recovery: if the settings are lost or forgotten, holding the BOOT button for 5 s at power-up resets Wi-Fi name, password and device name to the defaults (it keeps the speaker, volume and sounds), and the serial log says so.
- Keeping the same channel and max 2 clients. A setting change never changes the saved speaker.

## Acceptance criteria

1. Change name and password; board restarts; phone rejoins with the new password; old password no longer works (B14).
2. A 7-character password is refused; so are an empty name and a 33-character name.
3. After power cycles the new name and password persist; no filesystem or NVS error on serial (B10).
4. Hold BOOT 5 s at power-up restores defaults and keeps sounds, speaker and volume.
5. Posting the same values (or only a device name change) works; a device-name-only change need not drop the Wi-Fi link beyond the restart.
6. Password never appears in `/api/*` replies, the serial log, or the page source.
