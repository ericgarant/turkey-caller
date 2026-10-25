# F14 Call packs: moose now, deer later

**Story:** As a hunter, I pick Turkey, Moose or Deer at the top of the page and see only that animal's calls. Moose calls are long (7 to 25 s), so they live on the SD card, not in flash.

**Depends on:** F12 (WROVER-E and SD card). Firmware lands as its own PR right after F12; the web, mock, script and this spec come first and work against the contract below.

## Catalog contract

- Every sound has an `animal`: `turkey`, `moose` or `deer`. Missing means `turkey`. `GET /api/sounds` returns `[{id, name, animal, group, bytes, ms}]`.
- A **pack** is a folder on the SD card: `/packs/<pack>/index.json` plus that pack's WAVs (mono IMA ADPCM, 22.05 kHz, same format as F02). The index has the same schema as the flash `index.json`, plus `animal`, and an optional `hint` shown on the button.
- Merge order: built-ins (flash), then packs (alphabetical by folder), then uploads. Uploads stay in `/sounds/` and only show under "Your recordings"; packs never mix with them.
- **One id namespace** across built-ins, packs and uploads. An upload that collides with a built-in or pack id gets 409. A duplicate id inside a pack is skipped, logged on serial, first one wins.
- Pack clips are not capped at 10 s; the firmware accepts up to 60 s each. `kMaxSounds` goes from 40 to 64 (the array may live in PSRAM). The SD index read buffer stays at 4 KB or less (F10).
- No card, or an unreadable card: packs and uploads do not list. A pack with a missing file lists the other clips. Packs are read-only on the board; add or remove them from a Mac.

## Page

- Tabs **Turkey** and **Moose** are always shown; **Deer** appears once the board lists a deer call. The chosen tab is remembered on the phone (`localStorage`).
- Each tab shows its groups (`Moose calls` for the moose pack). With no calls for the animal, the tab says they live on the SD card and points to the Instructions.
- The turkey tab looks as before. Playing a call from another tab keeps working; the playing button lights up when its tab is open.

## Moose pack (6 clips)

Made from Eric's six MP3s with `tools/audio/make_moose_pack.sh` (never committed, like the Dindon clips). Output goes to `tools/audio/data-sd/packs/moose/`; copy `packs/` onto the card.

| id | Name | Source | Length | Hint |
|---|---|---|---|---|
| moose_cow_heat | Cow in heat | 01 Femelle chaleur | 23.9 s | Cow calling for a bull |
| moose_cow_moan | Cow moan | 02 Plainte femelle | 19.9 s | Plaintive cow call, draws a bull in |
| moose_bull_tending | Tending grunt | 03 Rot Mâle Tending | 6.4 s | Bull grunting while he tends a cow |
| moose_rake | Antler rake | 04 Rackling | 13.7 s | Raking brush and trees |
| moose_bull_grunt | Bull grunt | 05 Rot Mâle | 17.4 s | Bull rut grunting |
| moose_bull_trail | Trailing grunt | 06 Rot Mâle trail | 3.9 s | Bull following a cow's trail |

Total about 920 KB on the card. Deer adds a folder `/packs/deer/` and a script in the same shape.

## Acceptance criteria (QA row B20)

1. With the pack on the card, `/api/sounds` lists the 6 moose calls with `animal: "moose"`, `ms` within 0.1 s of ffprobe, after the built-ins.
2. The page shows the Turkey and Moose tabs; Moose lists the 6 calls; each plays to its end (up to 24 s) with no underruns.
3. With the card out, the board boots, the turkey calls play, and the Moose tab shows the SD card hint.
4. An upload named `moose_rake` gets 409. A pack with a duplicate id lists the first and logs the second.
5. Pulling the card while a moose call plays: it stops with a toast, no reset (F12 criterion 5).
6. The page stays under its 20 KB gzip budget.
