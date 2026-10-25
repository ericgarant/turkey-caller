#!/usr/bin/env bash
# Builds the SD card call packs (F14): mono IMA ADPCM WAV, 22.05 kHz (same format as the turkey
# calls), full length (no 10 s cap; the firmware takes up to 60 s), plus each pack's index.json.
# Run on your Mac:  cd tools/audio && ./make_call_packs.sh
# Input:  Eric's MP3s, one folder each under ./local/ (personal use, never committed, see SOURCES.md):
#   local/moose-calls/   from "MooseCalls"        (01-MOOSE COW 1.mp3 ... 10-DEER ROT 2.mp3)
#   local/moose-prerut/  from "MoosePreRutCalls"  (1- Renâclement ... 6- Buck agressif)
#   local/deer-calls/    from "DeerCalls"         (01_Femelle chaleur ... 06_Rot Mâle (trail))
#   local/               from "TurkeyCalls"       (01_Dindon_Purr ... 05_Dondon_KeeKee, the same MP3s
#                        the built-in Dindon pack is cut from; here they stay full length)
# Output: ./data-sd/packs/<pack>/  (copy the whole packs/ folder onto the SD card as /packs, FAT)
set -euo pipefail
cd "$(dirname "$0")"
FILTER="silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse,loudnorm=I=-14:TP=-1.0"
# The six DeerCalls rows match PR #29 (make_deer_pack.sh): same ids, names and hints.
# pack|id|source (folder/file prefix)|display name (32 bytes max)|hint (47 bytes max)
# A pack's calls are listed in this order. Ids are unique across all packs and the built-ins.
CALLS="
turkey|dindon_purr_full|./01_|Purr & clucks (full)|Whole track, soft and steady
turkey|dindon_yelp_full|./03_|Yelp (full)|Whole yelp track
turkey|dindon_fast_yelp_full|./04_|Fast yelp (full)|Whole fast yelp track
turkey|dindon_kee_kee_full|./05_|Kee-kee run (full)|Whole kee-kee run track
turkey|dindon_gobble_full|./02_|Gobble (full)|Whole gobble track, use with care
moose|moose_cow_1|moose-calls/01-|Cow call 1|Short cow call
moose|moose_cow_2|moose-calls/02-|Cow call 2|Cow call, medium length
moose|moose_cow_3|moose-calls/03-|Cow call 3|Cow call, medium length
moose|moose_cow_4|moose-calls/04-|Cow call 4|Cow call, medium length
moose|moose_cow_5|moose-calls/05-|Cow call 5|Long cow call series
moose|moose_cow_heat_long|moose-prerut/3-|Cow in heat (long)|Long cow-in-heat call, pulls a bull in
moose|moose_cow_unready|moose-prerut/2-|Cow not ready|Cow not yet receptive, pre-rut
moose|moose_young_bull|moose-prerut/4-|Young bull|Young bull grunting
moose|moose_mature_bull|moose-prerut/5-|Mature bull|Mature bull grunting
moose|moose_aggr_bull|moose-prerut/6-|Aggressive bull|Aggressive bull, challenges a rival
moose|moose_big_bull|moose-calls/06-|Big bull|Deep grunt of a big bull
moose|moose_snorting|moose-calls/07-|Snorting|Bull snorting
moose|moose_snort_prerut|moose-prerut/1-|Pre-rut snort|Pre-rut snort
deer|deer_doe_estrus|deer-calls/01_|Doe in estrus|Doe bleat in heat, draws a buck in
deer|deer_doe_bleat|deer-calls/02_|Doe bleat|Plaintive doe call, calm, all day
deer|deer_doe|moose-calls/08-|Doe call|Short doe bleat
deer|deer_tending_grunt|deer-calls/03_|Tending grunt|Buck grunting while he tends a doe
deer|deer_trail_grunt|deer-calls/06_|Trailing grunt|Buck following a doe's trail
deer|deer_buck_grunt|deer-calls/05_|Buck grunt|Rut grunting, long series
deer|deer_grunt_1|moose-calls/09-|Short grunt|Single short buck grunt
deer|deer_grunt_2|moose-calls/10-|Grunt series|Short series of buck grunts
deer|deer_rattle|deer-calls/04_|Antler rattle|Rattling antlers, mimics two bucks sparring
"
rm -rf data-sd/packs && mkdir -p data-sd/packs
ROWS="$(mktemp)"; trap 'rm -f "$ROWS"' EXIT
while IFS='|' read -r pack id src name hint; do
  [ -n "$id" ] || continue
  dir=${src%%/*}; pre=${src#*/}
  file=$(find "local/$dir" -maxdepth 1 -name "${pre}*.mp3" 2>/dev/null | sort | head -n 1)
  if [ -z "$file" ]; then echo "!! missing: local/$dir/${pre}*.mp3"; continue; fi
  mkdir -p "data-sd/packs/$pack"
  ffmpeg -nostdin -loglevel error -y -i "$file" -af "$FILTER" -ac 1 -ar 22050 -c:a adpcm_ima_wav "data-sd/packs/$pack/$id.wav" \
    || { echo "!! convert failed: $file"; rm -f "data-sd/packs/$pack/$id.wav"; continue; }
  printf '%s|%s|%s|%s\n' "$pack" "$id" "$name" "$hint" >>"$ROWS"
done <<<"$CALLS"
# Each index lists only the clips that converted; json.dumps escapes quotes and backslashes.
python3 - "$ROWS" <<'PY'
import json, sys, collections
packs = collections.OrderedDict()
for line in open(sys.argv[1], encoding="utf-8"):
    if not line.strip(): continue
    pack, i, n, h = line.rstrip("\n").split("|", 3)
    assert len(n.encode()) <= 32 and len(h.encode()) <= 47, i
    # Turkey pack clips join the Dindon pack group on the Turkey tab; moose and deer get their own group.
    group = "pack" if pack == "turkey" else pack
    packs.setdefault(pack, []).append({"id": i, "name": n, "animal": pack, "group": group, "hint": h})
for pack, rows in packs.items():
    text = json.dumps(rows, indent=2, ensure_ascii=False) + "\n"
    assert len(text.encode()) < 4096, pack + " index over 4 KB"
    open(f"data-sd/packs/{pack}/index.json", "w", encoding="utf-8").write(text)
PY
total=0
for d in data-sd/packs/*/; do
  echo; echo "$(basename "$d"):"; printf "  %-22s %8s %8s\n" file seconds bytes
  for f in "$d"*.wav; do
    [ -e "$f" ] || continue
    s=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f"); b=$(wc -c <"$f" | tr -d ' '); total=$((total+b))
    printf "  %-22s %8.1f %8d\n" "$(basename "$f")" "$s" "$b"
  done
done
echo; echo "TOTAL: $total bytes (~$((total/1024)) KB). Copy data-sd/packs to the SD card as /packs."
