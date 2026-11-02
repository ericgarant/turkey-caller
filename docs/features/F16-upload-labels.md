# F16 Name, animal and description for my recordings

**Story:** When I upload a call I recorded, I say which animal it is for, give it a name and a short description. It shows up on that animal's tab, under "Your recordings". Later I can rename it, fix the description or move it to another animal without uploading it again.

**Asked by:** Eric, 2 Nov 2026 ("why have we put YOUR RECORDING on the turkey page by default").

**Depends on:** F03 (uploads), F12 (SD card), F14 (the three animals).

## Contract

- **Sidecar:** each upload `/sounds/<id>.wav` on the card may have `/sounds/<id>.json`: `{"name":"...","animal":"turkey|moose|deer","hint":"..."}`. The audio is never rewritten for a label change.
- **Upload:** `POST /api/sounds` (multipart) takes optional text fields `animal`, `name` and `hint`, sent **before** the `file` part (the firmware reads them when the file starts). A field left out keeps its value: on a new upload, the defaults; on a replace, what the recording had. With no field at all, a replace keeps its sidecar.
- **Edit:** `POST /api/sounds/edit` with `id` and any of `name`, `hint`, `animal` (form fields). Rewrites the sidecar only. 404 for an unknown id, 400 for a built-in or pack call, 409 while that recording is still uploading, 503 with no card. Works while a call plays.
- **Delete:** `POST /api/sounds/delete` also removes the sidecar.
- **List:** `GET /api/sounds` returns uploads with `group: "mine"`, and `name`, `animal` and `hint` from the sidecar. An upload with no sidecar (all uploads before F16) is `turkey`, named from its id, with no hint. The box's screen (F15) lists it under that animal too.
- **Checks:** `animal` must be `turkey`, `moose` or `deer` (else 400, nothing saved). `name` is at most 32 bytes and `hint` at most 47 bytes of UTF-8 (an accented letter takes 2): longer text is cut at a character boundary, control characters become spaces, the ends are trimmed. An empty `name` means the default name from the id. The page should cap its inputs to the same sizes.

## Acceptance (QA row B23)

1. Upload a clip with `animal=moose`, a name and a description: it lists on the Moose tab under "Your recordings" with that name and description, and on the box's Moose list.
2. Edit it to `animal=deer` and a new name: it moves to the Deer tab on every phone without a reload of the page, and the audio plays the same.
3. Restart the board: name, animal and description are kept.
4. An upload from before F16 shows on the Turkey tab, named from its id.
5. `animal=elk` gets 400 and changes nothing. A 60-character name is cut to 32 bytes without a broken character.
6. Delete the recording: both `<id>.wav` and `<id>.json` are gone from the card.
