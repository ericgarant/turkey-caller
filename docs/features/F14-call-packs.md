# F14 Call packs: deer, moose and more

**Story:** As a hunter, I pick Turkey, Moose or Deer at the top of the page and see only that animal's calls. Deer and moose calls are long (up to 30 s), and the full-length turkey tracks run up to 50 s, so they live on the SD card, not in flash.

**Depends on:** F12 (WROVER-E and SD card). Firmware lands as its own PR right after F12; the web, mock, script and this spec come first and work against the contract below.

## Catalog contract

- Every sound has an `animal`: `turkey`, `moose` or `deer`. Missing means `turkey`. `GET /api/sounds` returns `[{id, name, animal, group, hint, bytes, ms}]` (`hint` optional, empty if the index has none).
- A **pack** is a folder on the SD card: `/packs/<pack>/index.json` plus that pack's WAVs (mono IMA ADPCM, 22.05 kHz, same format as F02). The index has the same schema as the flash `index.json`, plus `animal`, and an optional `hint` (up to 47 bytes) shown on the button.
- Merge order: built-ins (flash), then packs (alphabetical by folder), then uploads. Uploads stay in `/sounds/` and only show under "Your recordings"; packs never mix with them.
- **One id namespace** across built-ins, packs and uploads. An upload that collides with a built-in or pack id gets 409. A duplicate id inside a pack is skipped, logged on serial, first one wins.
- Pack clips are not capped at 10 s; the firmware accepts up to 60 s each. `kMaxSounds` goes from 40 to 64 and the `Sound` array lives in PSRAM. `Sound` gains `animal[8]` and `hint[48]`, and `group` grows past 6 bytes (`moose`, `deer` need room); `library.cpp` accepts the `pack`, `moose`, `deer` and `mine` groups from an SD index (`mine` is the page's "Your recordings" for uploads; `group[6]` still fits all of them).  The SD index read buffer stays at 4 KB or less (F10).
- A pack lists **only** the clips named in its `index.json` (unlike flash, there is no "Other sounds" fallback). A WAV without an entry is ignored.
- No card, or an unreadable card: packs and uploads do not list. A pack with a missing file lists the other clips. Packs are read-only on the board; add or remove them from a Mac.

## Page

- Tabs **Turkey** and **Moose** are always shown; **Deer** appears once the board lists a deer call. The chosen tab is remembered on the phone (`localStorage`).
- Each tab shows its groups (`Deer calls`, `Moose calls`). With no calls for the animal, the tab says they live on the SD card and points to the Instructions.
- The turkey tab looks as before. Playing a call from another tab keeps working; the playing button lights up when its tab is open.

## Packs (turkey, moose, deer)

Made from Eric's MP3s (folders TurkeyCalls, MooseCalls, MoosePreRutCalls and DeerCalls) with `tools/audio/make_call_packs.sh`. The clips are never committed, like the Dindon clips. The script's table is the source of truth for ids, names, hints and order; the tables below mirror it. Output goes to `tools/audio/data-sd/packs/<pack>/`; copy `packs/` onto the card. On 25 Oct 2026 Eric confirmed that the DeerCalls folder holds deer calls (a first draft had packed them as moose). Silence at the start and end of each track is trimmed, so some clips are shorter than their MP3s. 43 sounds in all with the 16 built-ins, of 64.

### Turkey pack: full-length Dindon tracks (group `pack`, next to the 10 s built-in versions), 5 clips, 1520 KB

| id | Name | Source | Length | Hint |
|---|---|---|---|---|
| dindon_purr_full | Purr & clucks (full) | TurkeyCalls: 01_Dindon_Purr | 41.0 s | Whole track, soft and steady |
| dindon_yelp_full | Yelp (full) | TurkeyCalls: 03_Dindon_Yelp1 | 16.6 s | Whole yelp track |
| dindon_fast_yelp_full | Fast yelp (full) | TurkeyCalls: 04_Dindon_Yelp2 | 21.0 s | Whole fast yelp track |
| dindon_kee_kee_full | Kee-kee run (full) | TurkeyCalls: 05_Dondon_KeeKee | 12.0 s | Whole kee-kee run track |
| dindon_gobble_full | Gobble (full) | TurkeyCalls: 02_Dindon_Glou | 50.0 s | Whole gobble track, use with care |

### Moose pack (group `moose`), 13 clips, 977 KB

| id | Name | Source | Length | Hint |
|---|---|---|---|---|
| moose_cow_1 | Cow call 1 | MooseCalls: 01-MOOSE COW 1 | 2.2 s | Short cow call |
| moose_cow_2 | Cow call 2 | MooseCalls: 02-MOOSE COW 2 | 9.1 s | Cow call, medium length |
| moose_cow_3 | Cow call 3 | MooseCalls: 03-MOOSE COW 3 | 7.7 s | Cow call, medium length |
| moose_cow_4 | Cow call 4 | MooseCalls: 04-MOOSE COW 4 | 6.2 s | Cow call, medium length |
| moose_cow_5 | Cow call 5 | MooseCalls: 05-MOOSE COW 5 | 20.3 s | Long cow call series |
| moose_cow_heat_long | Cow in heat (long) | MoosePreRutCalls: 3- Femelle en chaleur par Dan Lavoie Guide | 30.0 s | Long cow-in-heat call, pulls a bull in |
| moose_cow_unready | Cow not ready | MoosePreRutCalls: 2- Femelle non receptive par Dan Lavoie Guide | 5.7 s | Cow not yet receptive, pre-rut |
| moose_young_bull | Young bull | MoosePreRutCalls: 4- Jeune buck  | 1.7 s | Young bull grunting |
| moose_mature_bull | Mature bull | MoosePreRutCalls: 5- Buck mature  par Dan Lavoie Guide(1).mp3t | 2.2 s | Mature bull grunting |
| moose_aggr_bull | Aggressive bull | MoosePreRutCalls: 6- Buck  agressif par Dan Lavoie Guide(1) | 2.9 s | Aggressive bull, challenges a rival |
| moose_big_bull | Big bull | MooseCalls: 06-MOOSE BIG BULL | 0.5 s | Deep grunt of a big bull |
| moose_snorting | Snorting | MooseCalls: 07-MOOSE SNORTING | 0.9 s | Bull snorting |
| moose_snort_prerut | Pre-rut snort | MoosePreRutCalls: 1- Renâclement par Dan Lavoie Guide | 0.7 s | Pre-rut snort |

### Deer pack (group `deer`), 9 clips, 963 KB

| id | Name | Source | Length | Hint |
|---|---|---|---|---|
| deer_doe_estrus | Doe in estrus | DeerCalls: 01_Femelle chaleur | 23.9 s | Doe bleat in heat, draws a buck in |
| deer_doe_bleat | Doe bleat | DeerCalls: 02_Plaint femelle | 19.9 s | Plaintive doe call, calm, all day |
| deer_doe | Doe call | MooseCalls: 08-DEER DOE | 0.9 s | Short doe bleat |
| deer_tending_grunt | Tending grunt | DeerCalls: 03_Rot Mâle(Tending) | 6.4 s | Buck grunting while he tends a doe |
| deer_trail_grunt | Trailing grunt | DeerCalls: 06_Rot Mâle (trail) | 3.9 s | Buck following a doe's trail |
| deer_buck_grunt | Buck grunt | DeerCalls: 05_Rot Mâle | 17.4 s | Rut grunting, long series |
| deer_grunt_1 | Short grunt | MooseCalls: 09-DEER ROT 1 | 0.5 s | Single short buck grunt |
| deer_grunt_2 | Grunt series | MooseCalls: 10-DEER ROT 2 | 2.2 s | Short series of buck grunts |
| deer_rattle | Antler rattle | DeerCalls: 04_Rackling | 13.7 s | Rattling antlers, mimics two bucks sparring |

## Acceptance criteria (QA row B20)

1. With the packs on the card, `/api/sounds` lists the built-ins, then the turkey pack (5, `animal: "turkey"`, group `pack`), moose (13, `animal: "moose"`) and deer (9, `animal: "deer"`) in that order (packs alphabetical by folder, so deer comes before moose and turkey), with `ms` within 0.1 s of ffprobe.
2. The page shows Turkey and Moose tabs, plus Deer once a deer call is listed; Deer lists 9 calls and Moose lists 13; each plays to its end (up to 30 s) with no underruns.
2a. The Turkey tab lists the 5 full-length calls after the built-ins, under Dindon pack; each plays to its end (up to 50 s) with 0 underruns.
3. With the card out, the board boots, the turkey calls play, and the Moose tab shows the SD card hint.
4. An upload named `deer_rattle` gets 409. A pack with a duplicate id lists the first and logs the second.
5. Pulling the card while a deer or moose call plays: it stops with a toast, no reset (F12 criterion 5).
6. The page stays under its 20 KB gzip budget.
