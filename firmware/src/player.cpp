#include "player.h"

#include "heapmon.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <freertos/queue.h>

#include "library.h"
#include "sdcard.h"
#include "uploads.h"
#include "wav.h"

namespace player {
namespace {

// Ring buffer of mono 16-bit PCM at 44.1 kHz. 16 KB is ~0.19 s. ADPCM decodes far faster
// than real time, so this is plenty, and RAM is tight with Wi-Fi and a connected speaker.
constexpr size_t kRingBytes = 16 * 1024;
// Start sending a clip only once this much is buffered (~46 ms), so the first callback
// after a tap does not count as an underrun.
constexpr size_t kPrimeBytes = 4 * 1024;
constexpr size_t kReadChunk = 512;
constexpr int kSampleRate = 44100;

// Byte ring shared by the decode task (writer) and the Bluetooth callback (reader), which run
// on different cores. Every index update and the reset happen under one spinlock.
class Ring {
 public:
  bool begin(size_t size) {
    // Internal RAM on purpose: with PSRAM in the heap (WROVER-E) a plain malloc this size would
    // land in PSRAM, which is slower and shares the cache with flash reads (F12).
    buf_ = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_ = buf_ ? size : 0;
    return buf_ != nullptr;
  }
  size_t write(const uint8_t *src, size_t len) {
    portENTER_CRITICAL(&mux_);
    size_t n = min(len, size_ - count_);
    for (size_t i = 0; i < n; i++) {
      buf_[head_] = src[i];
      head_ = (head_ + 1 == size_) ? 0 : head_ + 1;
    }
    count_ += n;
    portEXIT_CRITICAL(&mux_);
    return n;
  }
  size_t read(uint8_t *dst, size_t len) {
    portENTER_CRITICAL(&mux_);
    size_t n = min(len, count_);
    for (size_t i = 0; i < n; i++) {
      dst[i] = buf_[tail_];
      tail_ = (tail_ + 1 == size_) ? 0 : tail_ + 1;
    }
    count_ -= n;
    portEXIT_CRITICAL(&mux_);
    return n;
  }
  bool internal() const { return buf_ && esp_ptr_internal(buf_); }
  void reset() {
    portENTER_CRITICAL(&mux_);
    head_ = tail_ = count_ = 0;
    portEXIT_CRITICAL(&mux_);
  }
  size_t available() {
    portENTER_CRITICAL(&mux_);
    size_t n = count_;
    portEXIT_CRITICAL(&mux_);
    return n;
  }

 private:
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  uint8_t *buf_ = nullptr;
  size_t size_ = 0, head_ = 0, tail_ = 0, count_ = 0;
};

struct Command {
  bool stop;
  bool loop;
  uint32_t preRollMs;
  char id[32];
};

Ring ring;
QueueHandle_t commands = nullptr;  // length 1, overwritten: the latest tap wins
File file;
bool fromCard = false;             // decode task only: the open clip is on the SD card
char openPath[56] = "";            // decode task only: path of the open clip, to reopen after a remount
char lastError[40] = "";           // "SD card removed", shown by the page once; under `lock`

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
char current[32] = "";
// Ownership: the decode task is the only writer of `playing`, `current`, `decodeDone` and `gen`.
// The A2DP callback (other core) only reads them, and reports back through `drainedGen`.
volatile bool aborting = false;    // closing the current clip: drop decoder output
volatile bool playing = false;     // a clip is being decoded or still draining from the ring
volatile bool decodeDone = true;   // the decoder has reached the end of the file
volatile uint32_t gen = 0;         // bumped for every clip started
volatile uint32_t drainedGen = 0;  // set by the callback: clip `gen` has fully played out
volatile uint8_t volume = 70;
volatile bool volumeDirty = false; // saved to NVS later, never during playback
uint32_t volumeChangedMs = 0;
volatile int32_t gainQ15 = 0;      // volume as a Q15 multiplier
volatile uint32_t plays = 0;
volatile uint32_t underruns = 0;
volatile uint32_t fills = 0;       // A2DP data callbacks so far
volatile bool stalled = false;     // the speaker stopped taking audio mid-clip
volatile uint32_t stalls = 0;
volatile bool looping = false;     // the current clip repeats (set at start and by setLoop())
volatile uint32_t lenMs = 0;       // length of the current clip
volatile uint32_t preRollFrames = 0;  // silence before the current clip (set before `playing`)
volatile uint32_t servedFrames = 0;  // callback-only writer: 44.1 kHz frames of the current clip sent
// Clips are WAV files: IMA ADPCM (what tools/audio produces, 4 bits a sample, no decoder
// memory) or plain 16-bit PCM, mono, any sample rate up to 44.1 kHz. The Helix MP3 decoder
// needed ~30 KB of heap, which with Wi-Fi and a connected speaker left the Bluetooth stack
// too little to keep streaming.
struct Clip {
  wav::Info info;
  bool loop = false;         // start again at the end until stopped or replaced
  uint32_t dataLeft = 0;     // bytes of the data chunk not read yet
  // resampler to 44.1 kHz (linear interpolation)
  uint32_t step = 0;         // input samples per output sample, Q16
  uint32_t phase = 0;        // Q16 position between prev and cur
  int16_t prev = 0, cur = 0;
  bool started = false;
} clip;

int16_t outBuf[256];
size_t outCount = 0;

void updateGain() {
  // Squared curve: sounds roughly linear to the ear and reaches 0 at 0.
  uint32_t v = volume;
  gainQ15 = (int32_t)((v * v * 32768UL) / 10000UL);
}

// True when decoder output should be thrown away: the clip is being closed or replaced.
bool shouldDrop() { return aborting || uxQueueMessagesWaiting(commands) > 0; }

// Pushes PCM into the ring. Blocks while the ring is full, but gives up as soon as a new
// command arrives so taps stay responsive. If nothing drains for 2 s the speaker has stopped
// pulling audio; give up on the clip instead of waiting forever.
void writePcm(const int16_t *pcm, size_t samples) {
  const uint8_t *p = (const uint8_t *)pcm;
  size_t left = samples * sizeof(int16_t);
  uint32_t waits = 0;
  while (left > 0) {
    if (shouldDrop() || stalled) return;
    size_t sent = ring.write(p, left);
    if (sent == 0) {
      if (++waits > 400) {
        stalled = true;
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(5));  // ~5 ms of audio drains meanwhile
      continue;
    }
    waits = 0;
    p += sent;
    left -= sent;
  }
}

void flushOut() {
  if (outCount) writePcm(outBuf, outCount);
  outCount = 0;
}

// One decoded input sample in; zero or more 44.1 kHz samples out.
void pushSample(int16_t x) {
  if (!clip.started) {
    clip.prev = clip.cur = x;
    clip.started = true;
  }
  clip.prev = clip.cur;
  clip.cur = x;
  while (clip.phase < 65536) {
    int32_t y = clip.prev + (((int32_t)(clip.cur - clip.prev) * (int32_t)clip.phase) >> 16);
    outBuf[outCount++] = (int16_t)y;
    if (outCount == sizeof(outBuf) / sizeof(outBuf[0])) flushOut();
    clip.phase += clip.step;
  }
  clip.phase -= 65536;
}

const int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int16_t kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

// Decodes one IMA ADPCM block (WAV layout, mono): predictor, step index, then 4-bit codes,
// low nibble first.
void decodeAdpcmBlock(const uint8_t *b, size_t len) {
  if (len < 4) return;
  int32_t pred = (int16_t)(b[0] | (b[1] << 8));
  int index = b[2] > 88 ? 88 : b[2];
  pushSample((int16_t)pred);
  for (size_t i = 4; i < len; i++) {
    for (int half = 0; half < 2; half++) {
      uint8_t code = half ? (b[i] >> 4) : (b[i] & 0x0F);
      int step = kStepTable[index];
      int32_t diff = step >> 3;
      if (code & 4) diff += step;
      if (code & 2) diff += step >> 1;
      if (code & 1) diff += step >> 2;
      pred += (code & 8) ? -diff : diff;
      if (pred > 32767) pred = 32767;
      if (pred < -32768) pred = -32768;
      index += kIndexTable[code];
      if (index < 0) index = 0;
      if (index > 88) index = 88;
      pushSample((int16_t)pred);
    }
  }
}

uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

void reportCardGone() {
  sdcard::markGone();
  portENTER_CRITICAL(&lock);
  strlcpy(lastError, "SD card removed", sizeof(lastError));
  portEXIT_CRITICAL(&lock);
}

// A card read failed. Before calling the card removed, remount it once: a single glitch (a dip
// on a battery pack, a card that stopped answering) otherwise needed a restart. Never while an
// upload is writing to the card. Returns true when the card is back.
bool tryRemount() {
  Serial.println("SD card: read failed, remounting");
  return uploads::remountCard();
}

// Reopens the clip on the card at byte `pos` after a remount, with the same small buffer.
bool reopenAt(uint32_t pos) {
  fs::FS *fs = sdcard::fs();
  if (!fs) return false;
  file = fs->open(openPath, "r");
  if (!file) return false;
  file.setBufferSize(128);
  return file.seek(pos);
}

// Reads the WAV header up to the data chunk. Returns false (and logs) on a file we can't play.
bool openWav(const char *path) {
  clip = Clip();
  const char *why = "";
  if (!wav::read(file, clip.info, &why)) {
    Serial.printf("can't play %s: %s\n", path, why);
    return false;
  }
  clip.dataLeft = clip.info.dataBytes;
  clip.step = (uint32_t)(((uint64_t)clip.info.rate << 16) / kSampleRate);
  return true;
}

// Decodes the next piece of the clip. Returns false at the end of the data.
bool decodeSome() {
  static uint8_t buf[wav::kMaxBlock];  // one ADPCM block (ffmpeg writes 1024-byte blocks)
  if (clip.dataLeft == 0) {
    if (!looping) return false;  // Repeat can be switched off mid-call: this pass is the last
    // Repeat: back to the first block. The resampler keeps its state, so the join is seamless.
    file.seek(clip.info.dataStart);
    clip.dataLeft = clip.info.dataBytes;
  }
  size_t want = clip.info.adpcm ? clip.info.blockAlign : kReadChunk;
  if (want > clip.dataLeft) want = clip.dataLeft;
  uint32_t pos = fromCard ? file.position() : 0;
  size_t n = file.read(buf, want);
  if (n < want && fromCard) {
    // Short read with data still to come: the card failed or was pulled (F12). Remount once and
    // carry on from the same spot; if the card doesn't come back, stop the call and tell the page.
    file.close();
    if (tryRemount() && reopenAt(pos)) n = file.read(buf, want);
    if (n < want) {
      if (file) file.close();
      reportCardGone();
      return false;
    }
  }
  if (n == 0) return false;
  clip.dataLeft -= n;
  if (clip.info.adpcm) {
    decodeAdpcmBlock(buf, n);
  } else {
    for (size_t i = 0; i + 1 < n; i += 2) pushSample((int16_t)rd16(buf + i));
  }
  return true;
}

void closeCurrent() {
  playing = false;  // the A2DP callback stops reading the ring
  aborting = true;
  if (file) file.close();
  outCount = 0;
  aborting = false;
  decodeDone = true;
  ring.reset();
  looping = false;
  portENTER_CRITICAL(&lock);
  current[0] = 0;
  portEXIT_CRITICAL(&lock);
}

void startClip(const char *id, bool loop, uint32_t preRollMs) {
  library::Sound snd;
  if (!library::find(id, snd)) {
    Serial.printf("can't play %s: unknown sound\n", id);
    return;
  }
  char path[56];
  library::path(snd, path, sizeof(path));  // flash, a pack or an upload (F14)
  fromCard = snd.card;
  strlcpy(openPath, path, sizeof(openPath));
  fs::FS *fs = fromCard ? sdcard::fs() : &LittleFS;
  // The card is marked gone after an earlier failure: a tap tries it again.
  if (!fs && fromCard && tryRemount()) fs = sdcard::fs();
  if (!fs) {
    Serial.printf("can't play %s: no SD card\n", id);
    portENTER_CRITICAL(&lock);
    strlcpy(lastError, "SD card removed", sizeof(lastError));
    portEXIT_CRITICAL(&lock);
    return;
  }
  file = fs->open(path, "r");
  if (!file && fromCard && tryRemount() && (fs = sdcard::fs())) file = fs->open(path, "r");
  if (!file) {
    // Opening a card file that was there at boot fails once the card is gone.
    if (fromCard) reportCardGone();
    Serial.printf("can't open %s\n", path);
    return;
  }
  // The C library would give the open file a 4 KB read buffer. The decoder already reads whole
  // 1 KB blocks, so a small one is enough and leaves that heap for the web server.
  file.setBufferSize(128);
  if (!openWav(path)) {
    file.close();
    return;
  }
  clip.loop = loop;
  looping = loop;
  lenMs = clip.info.ms();
  portENTER_CRITICAL(&lock);
  strlcpy(current, id, sizeof(current));
  portEXIT_CRITICAL(&lock);
  stalled = false;
  preRollFrames = preRollMs * (kSampleRate / 1000);
  gen = gen + 1;  // before `playing`, so the callback never pairs the new clip with an old gen
  decodeDone = false;
  playing = true;
  plays = plays + 1;
  Serial.printf("play %s heap=%u block=%u low=%u\n", id, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                (unsigned)heapmon::lowSince());
}

void task(void *) {
  Command cmd;
  for (;;) {
    // Decoding: poll so decoding keeps going. Draining: wake every 10 ms to notice the end.
    // Idle: sleep until a command arrives.
    TickType_t wait = file ? 0 : playing ? pdMS_TO_TICKS(10) : portMAX_DELAY;
    bool got = xQueueReceive(commands, &cmd, wait) == pdTRUE;
    if (got) {
      if (!cmd.stop) {
        portENTER_CRITICAL(&lock);
        lastError[0] = 0;  // a new tap clears the last card error, like the speaker's (#14)
        portEXIT_CRITICAL(&lock);
      }
      closeCurrent();
      if (!cmd.stop) startClip(cmd.id, cmd.loop, cmd.preRollMs);
      continue;
    }
    if (!file) {
      if (playing && decodeDone && drainedGen == gen) {
        playing = false;  // the clip has fully played out
        Serial.printf("done %s\n", current);
        portENTER_CRITICAL(&lock);
        current[0] = 0;
        portEXIT_CRITICAL(&lock);
      }
      continue;
    }
    if (stalled) {
      Serial.printf("speaker stopped taking audio (callbacks %u), dropping %s\n", (unsigned)fills, current);
      closeCurrent();
      stalled = false;
      stalls = stalls + 1;  // bt::loop() asks the speaker to restart the stream
      continue;
    }
    if (!decodeSome()) {
      flushOut();
      file.close();
      decodeDone = true;  // the A2DP callback clears `playing` once the ring drains
    }
  }
}

}  // namespace

void begin() {
  Preferences prefs;
  prefs.begin("player", true);
  volume = prefs.getUChar("volume", 70);
  prefs.end();
  updateGain();

  if (!ring.begin(kRingBytes)) log_e("no memory for the %u byte audio ring", (unsigned)kRingBytes);
  // F12: the ring and the decode buffers (static arrays) must be in internal RAM.
  Serial.printf("audio buffers in %s RAM\n", ring.internal() && esp_ptr_internal(outBuf) ? "internal" : "PSRAM (unexpected)");
  commands = xQueueCreate(1, sizeof(Command));
  // Core 1 (Arduino side); Bluetooth runs on core 0. Priority above loop() and the web server.
  xTaskCreatePinnedToCore(task, "player", 4096, nullptr, 5, nullptr, 1);
}

bool play(const char *id, bool loop, uint32_t preRollMs) {
  if (!id || !*id || strlen(id) >= sizeof(Command::id) || strchr(id, '/') || strchr(id, '.')) return false;
  Command cmd{};
  cmd.stop = false;
  cmd.loop = loop;
  cmd.preRollMs = preRollMs;
  strlcpy(cmd.id, id, sizeof(cmd.id));
  xQueueOverwrite(commands, &cmd);
  return true;
}

void setLoop(bool on) {
  if (playing) looping = on;
}

void stop() {
  Command cmd{};
  cmd.stop = true;
  xQueueOverwrite(commands, &cmd);
}

void setVolume(uint8_t level) {
  if (level > 100) level = 100;
  volume = level;
  updateGain();  // takes effect right away
  volumeChangedMs = millis();
  volumeDirty = true;
}

void loop() {
  // Flash writes stall flash reads on both cores, so save only once the slider has settled
  // and nothing is playing (see docs/architecture.md).
  if (volumeDirty && !playing && millis() - volumeChangedMs > 2000) {
    volumeDirty = false;
    Preferences prefs;
    prefs.begin("player", false);
    prefs.putUChar("volume", volume);
    prefs.end();
    Serial.printf("volume %u saved\n", (unsigned)volume);
  }
}

String error() {
  char copy[sizeof(lastError)];
  portENTER_CRITICAL(&lock);
  strlcpy(copy, lastError, sizeof(copy));
  portEXIT_CRITICAL(&lock);
  return String(copy);
}

Stats stats() {
  Stats s{};
  s.playing = playing;
  portENTER_CRITICAL(&lock);
  strlcpy(s.current, current, sizeof(s.current));
  portEXIT_CRITICAL(&lock);
  s.volume = volume;
  s.plays = plays;
  s.underruns = underruns;
  s.bufferedBytes = ring.available();
  s.bufferSize = kRingBytes;
  s.fills = fills;
  s.loop = s.playing && looping;
  s.lenMs = s.playing ? lenMs : 0;
  uint32_t pos = s.playing ? (uint32_t)((uint64_t)servedFrames * 1000 / kSampleRate) : 0;
  if (s.loop && s.lenMs) pos %= s.lenMs;
  s.posMs = pos;
  s.stalls = stalls;
  return s;
}

// Short ramps so a clip cut off by the next tap (or stopped) doesn't click.
constexpr int32_t kFadeSamples = 256;  // ~6 ms

int32_t fillFrames(Frame *frames, int32_t count) {
  static int16_t mono[512];
  static uint32_t primedGen = 0;  // callback-only: the clip that has passed its pre-buffer
  static uint32_t servedGen = 0;  // callback-only: the clip whose samples were sent last
  static int32_t fadeIn = 0;      // samples of the current clip sent so far, up to kFadeSamples
  static int16_t last = 0;        // last sample sent, ramped to 0 when a clip ends early
  static uint32_t preGen = 0, preLeft = 0;  // callback-only: pre-roll silence still to send
  int32_t done = 0;
  fills = fills + 1;
  uint32_t g = gen;  // read once: the clip this call is serving

  bool active = playing && drainedGen != g;
  if (active && primedGen != g) {
    if (ring.available() >= kPrimeBytes || decodeDone) primedGen = g;
  }

  // Volume steps ramp over ~6 ms (one Q15 step per sample at most 128), so a change never clicks.
  static int32_t gain = -1;
  if (gain < 0) gain = gainQ15;
  const int32_t target = gainQ15;
  if (active && primedGen == g) {
    if (preGen != g) {
      preGen = g;
      preLeft = preRollFrames;
    }
    for (; preLeft > 0 && done < count; preLeft--, done++) {
      frames[done].channel1 = 0;
      frames[done].channel2 = 0;
      last = 0;
    }
    while (done < count) {
      size_t want = min<size_t>(count - done, sizeof(mono) / sizeof(mono[0]));
      size_t got = ring.read((uint8_t *)mono, want * sizeof(int16_t)) / sizeof(int16_t);
      if (got == 0) break;
      if (servedGen != g) {
        servedGen = g;
        fadeIn = 0;
        servedFrames = 0;
      }
      for (size_t i = 0; i < got; i++) {
        if (gain != target) gain += constrain(target - gain, -128, 128);
        int32_t s = ((int32_t)mono[i] * gain) >> 15;
        if (fadeIn < kFadeSamples) s = s * fadeIn++ / kFadeSamples;
        frames[done + i].channel1 = (int16_t)s;
        frames[done + i].channel2 = (int16_t)s;
        last = (int16_t)s;
      }
      done += got;
      servedFrames = servedFrames + got;
    }
    if (done < count && g == gen) {
      if (decodeDone) {
        drainedGen = g;  // the decode task clears `playing`
      } else {
        underruns = underruns + 1;  // decoder fell behind: an audible dropout
      }
    }
  }

  // Silence, starting with a ramp from wherever the last clip stopped.
  for (int32_t i = done; i < count; i++) {
    int16_t s = 0;
    if (last != 0) {
      int32_t step = last / 32;
      if (step == 0) step = last > 0 ? 1 : -1;
      last = (int16_t)(last - step);
      s = last;
    }
    frames[i].channel1 = s;
    frames[i].channel2 = s;
  }
  return count;
}

}  // namespace player
