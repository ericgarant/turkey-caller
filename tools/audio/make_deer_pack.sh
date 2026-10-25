#!/usr/bin/env bash
# Builds the deer call pack for the SD card: mono IMA ADPCM WAV, 22.05 kHz (same format as the
# turkey calls), full length (no 10 s cap: deer calls here run 4 to 24 s), plus the pack's index.json.
# Run on your Mac:  cd tools/audio && ./make_deer_pack.sh
# Input:  ./local/deer/ with Eric's six MP3s (names start with 01_ to 06_, see SOURCES.md)
# Output: ./data-sd/packs/deer/  (copy the whole packs/ folder onto the SD card, FAT)
set -euo pipefail
cd "$(dirname "$0")"
FILTER="silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse,loudnorm=I=-14:TP=-1.0"
OUT_DIR="data-sd/packs/deer"
# id|file prefix|display name|hint
PACK="
deer_doe_estrus|01_|Doe in estrus|Doe bleat in heat, draws a buck in
deer_doe_bleat|02_|Doe bleat|Plaintive doe call, calm, all day
deer_tending_grunt|03_|Tending grunt|Buck grunting while he tends a doe
deer_rattle|04_|Antler rattle|Rattling antlers, mimics two bucks sparring
deer_buck_grunt|05_|Buck grunt|Rut grunting, long series
deer_trail_grunt|06_|Trailing grunt|Buck following a doe's trail
"
rm -rf "$OUT_DIR" && mkdir -p "$OUT_DIR"   # only this pack: other packs under data-sd/packs stay
ROWS="$(mktemp)"; trap 'rm -f "$ROWS"' EXIT
while IFS='|' read -r id pre name hint; do
  [ -n "$id" ] || continue
  src=$(find local/deer -maxdepth 1 -name "${pre}*.mp3" | head -n 1)
  if [ -z "$src" ]; then echo "!! missing: local/deer/${pre}*.mp3"; continue; fi
  ffmpeg -nostdin -loglevel error -y -i "$src" -af "$FILTER" -ac 1 -ar 22050 -c:a adpcm_ima_wav "$OUT_DIR/$id.wav" \
    || { echo "!! convert failed: $src"; rm -f "$OUT_DIR/$id.wav"; continue; }
  printf '%s|%s|%s\n' "$id" "$name" "$hint" >>"$ROWS"
done <<<"$PACK"
# The index lists only clips that converted; json.dumps escapes quotes and backslashes in names and hints.
python3 - "$ROWS" "$OUT_DIR/index.json" <<'PY'
import json, sys
rows = [l.rstrip("\n").split("|", 2) for l in open(sys.argv[1], encoding="utf-8") if l.strip()]
out = [{"id": i, "name": n, "animal": "deer", "group": "deer", "hint": h} for i, n, h in rows]
open(sys.argv[2], "w", encoding="utf-8").write(json.dumps(out, indent=2, ensure_ascii=False) + "\n")
PY
echo; printf "%-20s %8s %8s\n" file seconds bytes
total=0
for f in "$OUT_DIR"/*.wav; do
  [ -e "$f" ] || continue
  d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f"); b=$(wc -c <"$f" | tr -d ' '); total=$((total+b))
  printf "%-20s %8.1f %8d\n" "$(basename "$f")" "$d" "$b"
done
echo "TOTAL: $total bytes (~$((total/1024)) KB). Copy data-sd/packs to the SD card as /packs."
