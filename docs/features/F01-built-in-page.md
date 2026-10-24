# F01 Firmware serves the built-in page

**Story:** As a hunter, I join the board's Wi-Fi and the control page opens, with nothing else to install and no internet.

## Behavior

- `web/index.html` is gzipped by `web/build.py` into `firmware/src/web_page.h` and served at `/` with `Content-Encoding: gzip`. It replaces the Phase 0 test page in `firmware/page/`, which is removed (the stress test lives in `web/test/stress.mjs`).
- The page is part of the app binary, never in LittleFS, so `uploadfs` and uploads never change it and a page update never wipes sounds.
- The build runs `web/build.py` before compiling and fails if it fails or if the gzipped page is over 20 KB.
- Captive portal: any unknown host or path on the AP answers with the page (or a redirect to `http://192.168.4.1/`), so phones open it on joining.
- Every API route and the WebSocket in `docs/architecture.md` exists and matches `web/README.md` (field names, status codes, form-encoded POST bodies).
- `/api/status` reports `bt` as `idle`, `searching`, `connecting` or `connected`, and `version` (git describe or build date).
- Sending `Cache-Control: no-cache` with an ETag, so a reflashed page shows up on the next load.

## Acceptance criteria

1. Phone joins `TurkeyCaller`; the page loads in under 5 s (B1) and the header shows the firmware version.
2. All 12 calls are listed with names and groups; the three screens work against the real board.
3. `node web/test/api.mjs http://192.168.4.1` reports no mismatches (the `bt` string differences listed in the QA plan are gone).
4. After `pio run -t upload` (not `uploadfs`) with a changed page, the new page shows after a reload and all sounds are still there (B15).
5. The app binary stays under its size budget (F09) and lowest heap stays at or above 40 KB (F10) with the page open on two phones.
6. Pull the page out of the build (delete `web_page.h`) and the build regenerates it, or fails with a clear message.
