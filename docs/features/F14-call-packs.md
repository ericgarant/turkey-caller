# F14 Call packs: deer, moose and more

**Story:** As a hunter, I pick Turkey, Moose or Deer at the top of the page and see only that animal's calls. Deer and moose calls are long (4 to 25 s), so they live on the SD card, not in flash.

**Depends on:** F12 (WROVER-E and SD card). Firmware lands as its own PR right after F12; the web, mock, script and this spec come first and work against the contract below.

## Catalog contract

- Every sound has an `animal`: `turkey`, `moose` or `deer`. Missing means `turkey`. `GET /api/sounds` returns `[{id, name, animal, group, hint, bytes, ms}]` (`hint` optional, empty if the index has none).
- A **pack** is a folder on the SD card: `/packs/<pack>/index.json` plus that pack's WAVs (mono IMA ADPCM, 22.05 kHz, same format as F02). The index has the same schema as the flash `index.json`, plus `animal`, and an optional `hint` (up to 47 bytes) shown on the button.
- Merge order: built-ins (flash), then packs (alphabetical by folder), then uploads. Uploads stay in `/sounds/` and only show under "Your recordings"; packs never mix with them.
- **One id namespace** across built-ins, packs and uploads. An upload that collides with a built-in or pack id gets 409. A duplicate id inside a pack is skipped, logged on serial, first one wins.
- Pack clips are not capped at 10 s; the firmware accepts up to 60 s each. `kMaxSounds` goes from 40 to 64 and the `Sound` array lives in PSRAM. `Sound` gains `animal[8]` and `hint[48]`, and `group` grows past 6 bytes (`moose`, `deer` need room); `library.cpp` accepts the `moose`, `deer` and `mine` groups (`mine` is the page's "Your recordings" for uploads; `group[6]` still fits all of them).  The SD index read buffer stays at 4 KB or less (F10).
- A pack lists **only** the clips named in its `index.json` (unlike flash, there is no "Other sounds" fallback). A WAV without an entry is ignored.
- No card, or an unreadable card: packs and uploads do not list. A pack with a missing file lists the other clips. Packs are read-only on the board; add or remove them from a Mac.

## Page

- Tabs **Turkey** and **Moose** are always shown; **Deer** appears once the board lists a deer call. The chosen tab is remembered on the phone (`localStorage`).
- Each tab shows its groups (`Deer calls`, `Moose calls`). With no calls for the animal, the tab says they live on the SD card and points to the Instructions.
- The turkey tab looks as before. Playing a call from another tab keeps working; the playing button lights up when its tab is open.

## Deer pack (6 clips)

Eric's first six MP3s were deer calls (French names), first packed as moose by mistake. The script (`make_deer_pack.sh`) is the source of truth for names and hints; the table mirrors it. It makes the clips from Eric's six MP3s (never committed, like the Dindon clips). Output goes to `tools/audio/data-sd/packs/deer/`; copy `packs/` onto the card. The real moose pack (Eric's MooseCalls and MoosePreRutCalls folders) is built separately into `packs/moose/`, with ids starting `moose_`.

| id | Name | Source | Length | Hint |
|---|---|---|---|---|
| deer_doe_estrus | Doe in estrus | 01 Femelle chaleur | 23.9 s | Doe bleat in heat, draws a buck in |
| deer_doe_bleat | Doe bleat | 02 Plainte femelle | 19.9 s | Plaintive doe call, calm, all day |
| deer_tending_grunt | Tending grunt | 03 Rot Mâle Tending | 6.4 s | Buck grunting while he tends a doe |
| deer_rattle | Antler rattle | 04 Rackling | 13.7 s | Rattling antlers, mimics two bucks sparring |
| deer_buck_grunt | Buck grunt | 05 Rot Mâle | 17.4 s | Rut grunting, long series |
| deer_trail_grunt | Trailing grunt | 06 Rot Mâle trail | 3.9 s | Buck following a doe's trail |

Total about 920 KB on the card.

## Acceptance criteria (QA row B20)

1. With the pack on the card, `/api/sounds` lists the deer calls with `animal: "deer"`, `ms` within 0.1 s of ffprobe, after the built-ins.
2. The page shows Turkey and Moose tabs, plus Deer once a deer call is listed; Deer lists the 6 calls; each plays to its end (up to 24 s) with no underruns.
3. With the card out, the board boots, the turkey calls play, and the Moose tab shows the SD card hint.
4. An upload named `deer_rattle` gets 409. A pack with a duplicate id lists the first and logs the second.
5. Pulling the card while a deer or moose call plays: it stops with a toast, no reset (F12 criterion 5).
6. The page stays under its 20 KB gzip budget.
