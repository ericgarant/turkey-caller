# Turkey call audio: sources and licenses

## Recommended set (10 calls, one clean labeled clip each)
Kee_Kee_Run.mp3 was dropped (Oct 2026, Eric's call) to make room for the Dindon pack, which has its own kee-kee run.
Source page: https://www.dnr.sc.gov/wildlife/turkey/sound/turkeysound_index.html
Credit: "All audio courtesy National Wild Turkey Federation" (nwtf.org). Page footer: "(c) 2020 All rights reserved". No open license.
=> Fine for a personal device. Do NOT commit these files to a public repo; keep the repo private or git-ignore `data/*.mp3` and fetch with the script.

| ESP32 file | Call | Source file |
|---|---|---|
| cluck.mp3 | Cluck | Cluck.mp3 |
| putt.mp3 | Putt (alarm) | Putt.mp3 |
| tree_call.mp3 | Tree call | Tree_Call.mp3 |
| plain_yelp.mp3 | Plain yelp of hen | Yelp_Hen.mp3 |
| cutting.mp3 | Cutting (excited hen) | Cutting_Hen.mp3 |
| assembly_yelp.mp3 | Adult hen assembly call | Assembly_Call.mp3 |
| fly_down_cackle.mp3 | Fly-down cackle | Fly_Down_Cackle.mp3 |
| purr.mp3 | Purr | Purr.mp3 |
| cluck_purr.mp3 | Cluck and purr | Cluck_Purr.mp3 |
| gobble.mp3 | Gobbling | Gobbling.mp3 |

## Whine (Xeno-canto)
`whine.mp3` comes from one of these recordings. All three are *Meleagris gallopavo* and the audio is licensed
**CC BY-NC-SA 4.0** (https://creativecommons.org/licenses/by-nc-sa/4.0/), not ND, so an edited copy may be shared
under the same licence. Details from the Xeno-canto record as published on GBIF.

| | Recording | Recordist | Date, place | Xeno-canto labels |
|---|---|---|---|---|
| **default** | XC508128, https://xeno-canto.org/508128 | Will Sweet | 2019-11-16, Drakes Island, York County, Maine | call, walking; sex indeterminate |
| alternate | XC433074, https://xeno-canto.org/433074 | Phil Brown | 2018-05-28, Plum Island, Newbury, Massachusetts | call |
| alternate | XC538558, https://xeno-canto.org/538558 | Ken Blankenship | 2017-04-07, Sawmill Canyon, Huachuca Mountains, Arizona | call; male |

Select with `WHINE_XC` and `WHINE_START`. XC508128 is the default because it's a fall flock walking and calling, which is where hens whine. XC538558 is a male, so likely gobbles. None of the records says "whine" in a label, so the call type still needs checking by ear.

**Attribution line** (use for any shared copy):
"Wild Turkey call, XC508128 by Will Sweet, xeno-canto.org/508128, CC BY-NC-SA 4.0. Modified: cut from WHINE_START to 10 s max, silence trimmed, loudness normalised (-14 LUFS), downmixed to mono, re-encoded as 64 kbps MP3."
(Swap ID and recordist if an alternate is used.)

The clip stays git-ignored, like the rest of `data/`.

## Dindon pack (Eric's own MP3s, 5 calls)
Five tracks Eric supplied, album tag "DINDON" (French for turkey), 2014, stereo 44.1 kHz ~200 kbps MP3 with cover art.
The origin and licence are not in the files, so treat them like the NWTF set: personal use, never committed.
Put them in `tools/audio/local/` (git-ignored) and run the fetch script; it finds them by name.

Each track repeats one call several times with digital silence between takes. One take is cut (start and length
in the script's `LOCAL` list, found from the silence gaps), then the usual cleanup: silence trim, -14 LUFS, mono,
IMA ADPCM 22.05 kHz.

| ESP32 file | Shown as | Source track | Cut | Clip | What it is (by analysis, confirm by ear) |
|---|---|---|---|---|---|
| dindon_purr.wav | Purr & clucks | 01_Dindon_Purr.mp3 (44 s) | 2.9 s + 3.9 s | 3.7 s, 41 KB | Soft purr, a single cluck, soft purr |
| dindon_yelp.wav | Yelp | 03_Dindon_Yelp1.mp3 (19 s) | 6.2 s + 3.3 s | 3.2 s, 35 KB | Plain hen yelp, about 8 notes, steady pace |
| dindon_fast_yelp.wav | Fast yelp | 04_Dindon_Yelp2.mp3 (24 s) | 2.5 s + 3.2 s | 3.1 s, 34 KB | Louder, faster, nearly continuous yelping (excited yelp) |
| dindon_kee_kee.wav | Kee-kee run | 05_Dondon_KeeKee.mp3 (15 s) | 2.55 s + 3.9 s | 3.8 s, 42 KB | High whistles (~1.6 kHz) ending in yelps |
| dindon_gobble.wav | Gobble | 02_Dindon_Glou.mp3 (56 s, "GlouGlou") | 5.0 s + 3.2 s | 2.9 s, 33 KB | One long gobble |

Total 181 KB (~17 s of audio). The full tracks would be ~1.7 MB as ADPCM, more than the whole 1.375 MB LittleFS.

Flash budget, from the board (Oct 2026): the 12 built-ins took 1,323,008 of 1,441,792 bytes (118,784 free; most
clips are capped at 10 s, ~111 KB each; LittleFS adds ~4.4 KB per file). The pack (181 KB + overhead ~ 203 KB) did not
fit, so the built-in kee-kee run (110,830 bytes) was dropped and the purr cut to one 3.7 s take. Expected free after:
about 27 KB.

### When to use them
| Call | Meaning | When hunters use it |
|---|---|---|
| Purr & clucks | Contented, feeding bird; a spacing call ("this is my space") [1][2] | Bird close and wary, inside ~100 yd: keep calling to soft purrs and clucks to reassure it the last steps [2][3]. Works spring and fall; good on pressured birds [4] |
| Yelp | The basic hen call; locator and assembly call, and a hen ready to breed in spring [1][2] | Spring: talk to a gobbler. Fall: gather a flock. Start soft, get louder only until birds answer [3] |
| Fast yelp | Excited yelp: faster, louder than a plain yelp. Excited, not alarmed [2] | A henned-up gobbler (draw the boss hen and he follows), or one that hung up after soft calling [2]. Match the birds' energy [3] |
| Kee-kee run | Lost call of young-of-the-year birds: whistles then yelps [2][4] | Fall, to reassemble a scattered flock: wait ~20 min, start soft and get louder [4]. Also adds realism in spring, on public land [2] |
| Gobble | The tom's mating call, spring [1][2] | Last resort for a tom that won't close the distance, or late evening to get a roosted tom to gobble [2][3]. Can draw other hunters: avoid on public land [2][3][5] |

[1] MeatEater, "The 6 Turkey Vocalizations You Need to Know", https://www.themeateater.com/hunt/wild-turkey/the-six-turkey-vocalizations-you-need-to-know
[2] NWTF, "The Sounds of the Wild Turkey", https://live.nwtf.org/content-hub/the-sounds-of-the-wild-turkey
[3] Outdoor Life, "Turkey calling tips", https://www.outdoorlife.com/hunting/turkey-calling-tips/
[4] Tennessee Wildlife Resources Agency, "Fall Calls that Work" (2019), https://www.tn.gov/twra/twra-outdoors-blog/2019/9/9/fall-turkey-calls-that-work.html
[5] Realtree, "The 10 Commandments of Public Land Turkey Hunting", https://realtree.com/turkey-hunting/articles/the-10-commandments-of-public-land-turkey-hunting

## Gaps
- Gobbler yelp / jake yelp, kee-kee alone, fighting purr: not in either set.

## Openly licensed alternative (for a public repo)
Xeno-canto has ~130 Meleagris gallopavo recordings, mostly CC BY-NC-SA (attribution, non-commercial).
https://xeno-canto.org/species/Meleagris-gallopavo
They are field recordings: you must listen and cut each call by hand, and labels are often just "call"/"song".
The API (v3) needs a free account key. Macaulay Library (Cornell) does not allow redistribution.

## Format and flash budget (ESP32-WROOM-32E, 4 MB flash)
- Default: MP3 mono 64 kbps 44.1 kHz ~ 8 KB/s. 11 clips x ~6 s ~ 0.5 MB. Fits the LittleFS partition defined in `firmware/partitions.csv` (that file is the source of truth for its size).
- 44.1 kHz is chosen because A2DP/SBC to the speaker runs at 44.1 kHz, so no resampling on the ESP32.
- WAV mono 16-bit 22.05 kHz ~ 44 KB/s: 11 clips ~ 2.9 MB, too big next to the firmware. Only use WAV for a few short clips.
- MP3 needs a decoder on the ESP32 (e.g. libhelix in arduino-audio-tools / ESP32-A2DP).

## Moose and deer packs (Eric's own MP3s, on the SD card)
22 tracks Eric supplied from three folders, 0.7 to 36 s. Origin and licence are not in the files: personal use, never
committed. Copy each folder's MP3s into its own folder under `tools/audio/local/`, then run
`tools/audio/make_call_packs.sh`:

| Eric's folder | Local folder | Goes to |
|---|---|---|
| MooseCalls | `local/moose-calls/` | 01 to 07 moose, 08 to 10 deer (by file name) |
| MoosePreRutCalls | `local/moose-prerut/` | moose |
| DeerCalls | `local/deer-calls/` | deer (Eric confirmed, 25 Oct) |

Same cleanup as the others (silence trim, -14 LUFS, mono, IMA ADPCM 22.05 kHz), but no 10 s cap, so the clips are kept
whole. The ids, names, hints and order are in the script's table. Output: `tools/audio/data-sd/packs/moose/` (13 calls)
and `tools/audio/data-sd/packs/deer/` (9 calls), about 1.9 MB together (git-ignored). Copy `packs/` to the SD card as
`/packs/`. See `docs/features/F14-call-packs.md`.

