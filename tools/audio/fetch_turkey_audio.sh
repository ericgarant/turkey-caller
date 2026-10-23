#!/usr/bin/env bash
# Downloads wild turkey call clips and converts them to small ESP32-friendly files.
# Run on your Mac:  brew install ffmpeg   then   cd tools/audio && ./fetch_turkey_audio.sh
# Output: ./raw (originals) and ./data (converted, ready for the ESP32 LittleFS image)
#
# FORMAT: mp3 (default, mono 64 kbps 44.1 kHz, ~8 KB/s) or wav (mono 16-bit 22.05 kHz, ~44 KB/s)
# MAXSEC: longest clip kept, in seconds (default 10)
set -euo pipefail
FORMAT="${FORMAT:-mp3}"
MAXSEC="${MAXSEC:-10}"
# Whine: not in the SC DNR set, taken from a Xeno-canto field recording (CC licence, see SOURCES.md).
# Listen to data/whine.mp3; if it's wrong, pick another recording or start time:
#   WHINE_XC=433074 WHINE_START=12 ./fetch_turkey_audio.sh
WHINE_XC="${WHINE_XC:-538558}"
WHINE_START="${WHINE_START:-0}"
cd "$(dirname "$0")"   # always write raw/ and data/ next to this script (git-ignored)
BASE="https://www.dnr.sc.gov/wildlife/turkey/sound"

# name|source file   (audio courtesy National Wild Turkey Federation, via SC DNR)
CLIPS="
cluck|Cluck.mp3
putt|Putt.mp3
tree_call|Tree_Call.mp3
plain_yelp|Yelp_Hen.mp3
cutting|Cutting_Hen.mp3
assembly_yelp|Assembly_Call.mp3
fly_down_cackle|Fly_Down_Cackle.mp3
kee_kee_run|Kee_Kee_Run.mp3
purr|Purr.mp3
cluck_purr|Cluck_Purr.mp3
gobble|Gobbling.mp3
"

# trim leading/trailing silence, normalise loudness (applied to every clip)
FILTER="silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse,loudnorm=I=-14:TP=-1.0"

if [ "$FORMAT" = wav ]; then OUT=(-ac 1 -ar 22050 -c:a pcm_s16le); else OUT=(-ac 1 -ar 44100 -c:a libmp3lame -b:a 64k); fi

# is_audio FILE: true only if ffprobe finds an audio stream (rejects HTML bot-check pages saved as .mp3)
is_audio() { ffprobe -v error -select_streams a -show_entries stream=codec_type -of csv=p=0 "$1" 2>/dev/null | grep -q audio; }

# fetch URL RAWFILE: download unless a valid cached copy exists; never keep a non-audio file
fetch() {
  if [ -s "$2" ] && is_audio "$2"; then return 0; fi
  rm -f "$2"
  curl -fsSL "$1" -o "$2.part" && is_audio "$2.part" && mv "$2.part" "$2" && return 0
  rm -f "$2.part"; return 1
}

# convert RAWFILE NAME [START]: on failure delete the raw file so the next run downloads it again
convert() {
  ffmpeg -loglevel error -y -ss "${3:-0}" -i "$1" -af "$FILTER" -t "$MAXSEC" "${OUT[@]}" "data/$2.$FORMAT" \
    || { echo "!! convert failed: $1"; rm -f "$1"; return 1; }
}

mkdir -p raw data
for line in $CLIPS; do
  name="${line%%|*}"; src="${line##*|}"
  fetch "$BASE/$src" "raw/$src" || { echo "!! failed: $src ($BASE/$src)"; continue; }
  convert "raw/$src" "$name" || continue
done

# whine from Xeno-canto (cut from WHINE_START, then same cleanup as the others)
wsrc="raw/XC$WHINE_XC.mp3"
if fetch "https://xeno-canto.org/$WHINE_XC/download" "$wsrc"; then
  convert "$wsrc" whine "$WHINE_START" || true
else
  echo "!! failed: $wsrc. Download it in a browser from https://xeno-canto.org/$WHINE_XC and save it as tools/audio/$wsrc"
fi

echo; printf "%-20s %8s %8s\n" file seconds bytes
total=0; count=0
for f in data/*."$FORMAT"; do
  [ -e "$f" ] || continue
  count=$((count+1))
  d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f")
  b=$(wc -c <"$f" | tr -d ' '); total=$((total+b))
  printf "%-20s %8.1f %8d\n" "$(basename "$f")" "$d" "$b"
done
[ "$count" -gt 0 ] || { echo "No $FORMAT clips in data/ (all downloads failed?)"; exit 1; }
echo "TOTAL: $count clips, $total bytes (~$((total/1024)) KB)"
