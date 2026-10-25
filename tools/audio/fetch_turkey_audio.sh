#!/usr/bin/env bash
# Downloads wild turkey call clips and converts them to small ESP32-friendly files.
# Run on your Mac:  brew install ffmpeg   then   cd tools/audio && ./fetch_turkey_audio.sh
# Output: ./raw (originals) and ./data (converted, ready for the ESP32 LittleFS image)
# Dindon pack: put Eric's five "Dindon" MP3s in ./local first (any of their file names work, see LOCAL below).
#
# FORMAT: adpcm (default: mono IMA ADPCM WAV, 22.05 kHz, ~11 KB/s; what the firmware plays),
#         wav (mono 16-bit PCM 22.05 kHz, ~44 KB/s; also plays) or mp3 (64 kbps, for listening only)
# MAXSEC: longest clip kept, in seconds (default 10)
set -euo pipefail
FORMAT="${FORMAT:-adpcm}"
MAXSEC="${MAXSEC:-10}"
# Whine: not in the SC DNR set, taken from a Xeno-canto field recording (CC licence, see SOURCES.md).
# Listen to data/whine.wav; if it's wrong, pick another recording or start time:
#   WHINE_XC=433074 WHINE_START=12 ./fetch_turkey_audio.sh
WHINE_XC="${WHINE_XC:-508128}"
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

case "$FORMAT" in
  adpcm) OUT=(-ac 1 -ar 22050 -c:a adpcm_ima_wav); EXT=wav ;;
  wav)   OUT=(-ac 1 -ar 22050 -c:a pcm_s16le); EXT=wav ;;
  mp3)   OUT=(-ac 1 -ar 44100 -c:a libmp3lame -b:a 64k); EXT=mp3 ;;
  *) echo "FORMAT must be adpcm, wav or mp3"; exit 1 ;;
esac

# is_audio FILE: true only if ffprobe finds an audio stream (rejects HTML bot-check pages saved as .mp3)
is_audio() { ffprobe -v error -select_streams a -show_entries stream=codec_type -of csv=p=0 "$1" 2>/dev/null | grep -q audio; }

# fetch URL RAWFILE: download unless a valid cached copy exists; never keep a non-audio file
fetch() {
  if [ -s "$2" ] && is_audio "$2"; then return 0; fi
  rm -f "$2"
  curl -fsSL "$1" -o "$2.part" && is_audio "$2.part" && mv "$2.part" "$2" && return 0
  rm -f "$2.part"; return 1
}

# convert RAWFILE NAME [START] [LENGTH]: LENGTH cuts the input (before silence trimming), else MAXSEC caps
# the output. On failure delete the raw file so the next run downloads it again
convert() {
  local cut=()
  [ -n "${4:-}" ] && cut=(-t "$4")
  ffmpeg -loglevel error -y -ss "${3:-0}" "${cut[@]}" -i "$1" -af "$FILTER" -t "$MAXSEC" "${OUT[@]}" "data/$2.$EXT" \
    || { echo "!! convert failed: $1"; case "$1" in raw/*) rm -f "$1" ;; esac; return 1; }
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

# Dindon pack: Eric's own MP3s (album "DINDON", 2014), not downloadable, so they are read from ./local
# (git-ignored like raw/ and data/). Each track repeats its call several times with gaps; one take is cut.
# id|file name pattern|start s|length s   (cut points found by silence gaps, see SOURCES.md)
LOCAL="
dindon_purr|*[Pp]urr*.mp3|2.9|7.7
dindon_gobble|*[Gg]lou*.mp3|5.0|3.2
dindon_yelp|*[Yy]elp1*.mp3|6.2|3.3
dindon_fast_yelp|*[Yy]elp2*.mp3|2.5|3.2
dindon_kee_kee|*[Kk]ee[Kk]ee*.mp3|2.55|3.9
"
mkdir -p local
for line in $LOCAL; do
  IFS='|' read -r name pat start len <<<"$line"
  src=$(find local -maxdepth 1 -name "$pat" | head -n 1)
  if [ -z "$src" ]; then echo "!! missing: local/$pat (copy the Dindon MP3s into tools/audio/local)"; continue; fi
  convert "$src" "$name" "$start" "$len" || true
done

echo; printf "%-20s %8s %8s\n" file seconds bytes
total=0; count=0
for f in data/*."$EXT"; do
  [ -e "$f" ] || continue
  count=$((count+1))
  d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f")
  b=$(wc -c <"$f" | tr -d ' '); total=$((total+b))
  printf "%-20s %8.1f %8d\n" "$(basename "$f")" "$d" "$b"
done
[ "$count" -gt 0 ] || { echo "No .$EXT clips in data/ (all downloads failed?)"; exit 1; }
echo "TOTAL: $count clips, $total bytes (~$((total/1024)) KB)"
