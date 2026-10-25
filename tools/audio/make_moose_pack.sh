#!/usr/bin/env bash
# Builds the moose call pack for the SD card: mono IMA ADPCM WAV, 22.05 kHz (same format as the
# turkey calls), full length (no 10 s cap: moose calls run 7 to 30 s), plus the pack's index.json.
# Run on your Mac:  cd tools/audio && ./make_moose_pack.sh
# Input:  ./local/moose/ with Eric's six MP3s (names start with 01_ to 06_, see SOURCES.md)
# Output: ./data-sd/packs/moose/  (copy the whole packs/ folder onto the SD card, FAT)
set -euo pipefail
cd "$(dirname "$0")"
FILTER="silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse,loudnorm=I=-14:TP=-1.0"
OUT_DIR="data-sd/packs/moose"
# id|file prefix|display name|hint
PACK="
moose_cow_heat|01_|Cow in heat|Cow calling for a bull, long and loud
moose_cow_moan|02_|Cow moan|Plaintive cow call, draws a bull in
moose_bull_tending|03_|Tending grunt|Bull grunting while he tends a cow
moose_rake|04_|Antler rake|Raking brush and trees, challenges a bull
moose_bull_grunt|05_|Bull grunt|Bull rut grunting, long series
moose_bull_trail|06_|Trailing grunt|Bull following a cow's trail
"
rm -rf data-sd && mkdir -p "$OUT_DIR"
json="["; sep=""
while IFS='|' read -r id pre name hint; do
  [ -n "$id" ] || continue
  src=$(find local/moose -maxdepth 1 -name "${pre}*.mp3" | head -n 1)
  if [ -z "$src" ]; then echo "!! missing: local/moose/${pre}*.mp3"; continue; fi
  ffmpeg -nostdin -loglevel error -y -i "$src" -af "$FILTER" -ac 1 -ar 22050 -c:a adpcm_ima_wav "$OUT_DIR/$id.wav" \
    || { echo "!! convert failed: $src"; continue; }
  json="$json$sep
  {\"id\": \"$id\", \"name\": \"$name\", \"animal\": \"moose\", \"group\": \"moose\", \"hint\": \"$hint\"}"
  sep=","
done <<<"$PACK"
printf '%s\n]\n' "$json" >"$OUT_DIR/index.json"
echo; printf "%-20s %8s %8s\n" file seconds bytes
total=0
for f in "$OUT_DIR"/*.wav; do
  [ -e "$f" ] || continue
  d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f"); b=$(wc -c <"$f" | tr -d ' '); total=$((total+b))
  printf "%-20s %8.1f %8d\n" "$(basename "$f")" "$d" "$b"
done
echo "TOTAL: $total bytes (~$((total/1024)) KB). Copy data-sd/packs to the SD card as /packs."
