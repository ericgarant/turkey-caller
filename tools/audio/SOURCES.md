# Turkey call audio: sources and licenses

## Recommended set (11 calls, one clean labeled clip each)
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
| kee_kee_run.mp3 | Kee-kee run | Kee_Kee_Run.mp3 |
| purr.mp3 | Purr | Purr.mp3 |
| cluck_purr.mp3 | Cluck and purr | Cluck_Purr.mp3 |
| gobble.mp3 | Gobbling | Gobbling.mp3 |

## Whine (Xeno-canto)
| Field | Value |
|---|---|
| ESP32 file | whine.mp3 |
| Recording | XC538558, https://xeno-canto.org/538558 (change with `WHINE_XC` / `WHINE_START`) |
| Species | TO CONFIRM on the page: *Meleagris gallopavo* |
| Recordist | TO CONFIRM (copy the name from the recording page) |
| Licence | TO CONFIRM (exact name and version, with link, e.g. CC BY-NC-SA 4.0, https://creativecommons.org/licenses/by-nc-sa/4.0/) |
| Changes | Modified: cut from `WHINE_START` to 10 s max, silence trimmed, loudness normalised (-14 LUFS), downmixed to mono, re-encoded as 64 kbps MP3 |

This recording was picked without listening (the sandbox can't open xeno-canto), so the species, the call type and the details above still need checking on the page. Alternates: XC433074, XC508128.

Licence rules for the edited clip:
- **BY-NC-ND**: the trimmed and normalised clip is an adaptation, which ND forbids sharing. Use it on your own device only, never commit or share it, or pick a non-ND recording instead.
- **BY-NC-SA**: any shared copy of the edited clip must carry the same licence and the attribution above.
- Either way the clip stays git-ignored, like the rest of `data/`.

## Gaps
- Gobbler yelp / jake yelp, kee-kee alone, fighting purr: not in this set.

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
