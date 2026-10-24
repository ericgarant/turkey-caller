#include "player.h"

#include <LittleFS.h>
#include <MP3DecoderHelix.h>
#include <Preferences.h>
#include <freertos/queue.h>

namespace player {
namespace {

// Ring buffer of mono 16-bit PCM at 44.1 kHz. 32 KB is ~0.37 s, the most we can spare
// without PSRAM once Wi-Fi and Bluetooth Classic are both up.
constexpr size_t kRingBytes = 32 * 1024;
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
    buf_ = (uint8_t *)malloc(size);
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
  char id[32];
};

Ring ring;
QueueHandle_t commands = nullptr;  // length 1, overwritten: the latest tap wins
libhelix::MP3DecoderHelix mp3;
File file;

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
char current[32] = "";
volatile bool aborting = false;   // closing the current clip: drop decoder output
volatile bool playing = false;     // a clip is being decoded or still draining from the ring
volatile bool decodeDone = true;   // the decoder has reached the end of the file
volatile bool primed = false;      // the ring has filled enough to start this clip
volatile uint8_t volume = 70;
volatile int32_t gainQ15 = 0;      // volume as a Q15 multiplier
volatile uint32_t plays = 0;
volatile uint32_t underruns = 0;
bool warnedRate = false;

void updateGain() {
  // Squared curve: sounds roughly linear to the ear and reaches 0 at 0.
  uint32_t v = volume;
  gainQ15 = (int32_t)((v * v * 32768UL) / 10000UL);
}

// True when decoder output should be thrown away: the clip is being closed or replaced.
bool shouldDrop() { return aborting || uxQueueMessagesWaiting(commands) > 0; }

// Decoder output: downmix to mono and push into the ring. Blocks while the ring is full,
// but gives up as soon as a new command arrives so taps stay responsive. Waiting matters only
// when the speaker is slower than the decoder; if the speaker is gone the web layer refuses plays.
void onPcm(MP3FrameInfo &info, short *pcm, size_t samples, void *) {
  if (info.samprate != kSampleRate && !warnedRate) {
    warnedRate = true;
    log_w("clip is %d Hz, expected %d Hz: it will play at the wrong speed", info.samprate, kSampleRate);
  }
  size_t frames = samples;
  if (info.nChans == 2) {
    frames = samples / 2;
    for (size_t i = 0; i < frames; i++) {
      pcm[i] = (short)(((int32_t)pcm[2 * i] + pcm[2 * i + 1]) / 2);
    }
  }
  const uint8_t *p = (const uint8_t *)pcm;
  size_t left = frames * sizeof(short);
  while (left > 0) {
    if (shouldDrop()) return;
    size_t sent = ring.write(p, left);
    if (sent == 0) {
      vTaskDelay(pdMS_TO_TICKS(5));  // ring full: ~5 ms of audio drains meanwhile
      continue;
    }
    p += sent;
    left -= sent;
  }
}

void closeCurrent() {
  playing = false;  // the A2DP callback stops reading the ring
  aborting = true;
  if (file) {
    mp3.end();  // end() flushes the decoder; onPcm drops that output
    file.close();
  }
  aborting = false;
  decodeDone = true;
  primed = false;
  ring.reset();
  portENTER_CRITICAL(&lock);
  current[0] = 0;
  portEXIT_CRITICAL(&lock);
}

void startClip(const char *id) {
  char path[48];
  snprintf(path, sizeof(path), "/sounds/%s.mp3", id);
  file = LittleFS.open(path, "r");
  if (!file) {
    log_e("cannot open %s", path);
    return;
  }
  mp3.begin();
  portENTER_CRITICAL(&lock);
  strlcpy(current, id, sizeof(current));
  portEXIT_CRITICAL(&lock);
  primed = false;
  decodeDone = false;
  playing = true;
  plays = plays + 1;
  log_i("play %s (%u bytes)", path, (unsigned)file.size());
}

void task(void *) {
  static uint8_t buf[kReadChunk];
  Command cmd;
  for (;;) {
    // Idle: wait for a command. Playing: only peek, so decoding keeps going.
    bool got = file ? xQueueReceive(commands, &cmd, 0) == pdTRUE
                    : xQueueReceive(commands, &cmd, portMAX_DELAY) == pdTRUE;
    if (got) {
      closeCurrent();
      if (!cmd.stop) startClip(cmd.id);
      continue;
    }
    if (!file) continue;
    size_t n = file.read(buf, sizeof(buf));
    if (n > 0) {
      mp3.write(buf, n);
    } else {
      mp3.end();  // flush the last frames
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
  commands = xQueueCreate(1, sizeof(Command));
  mp3.setDataCallback(onPcm);
  // Core 1 (Arduino side); Bluetooth runs on core 0. Priority above loop() and the web server.
  xTaskCreatePinnedToCore(task, "player", 6144, nullptr, 5, nullptr, 1);
}

bool play(const char *id) {
  if (!id || !*id || strlen(id) >= sizeof(Command::id) || strchr(id, '/') || strchr(id, '.')) return false;
  Command cmd{};
  cmd.stop = false;
  strlcpy(cmd.id, id, sizeof(cmd.id));
  xQueueOverwrite(commands, &cmd);
  return true;
}

void stop() {
  Command cmd{};
  cmd.stop = true;
  xQueueOverwrite(commands, &cmd);
}

void setVolume(uint8_t level) {
  if (level > 100) level = 100;
  volume = level;
  updateGain();
  Preferences prefs;
  prefs.begin("player", false);
  prefs.putUChar("volume", level);
  prefs.end();
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
  return s;
}

int32_t fillFrames(Frame *frames, int32_t count) {
  static int16_t mono[512];
  int32_t done = 0;

  if (playing && !primed) {
    if (ring.available() >= kPrimeBytes || decodeDone) primed = true;
  }

  if (playing && primed) {
    int32_t gain = gainQ15;
    while (done < count) {
      size_t want = min<size_t>(count - done, sizeof(mono) / sizeof(mono[0]));
      size_t got = ring.read((uint8_t *)mono, want * sizeof(int16_t)) / sizeof(int16_t);
      if (got == 0) break;
      for (size_t i = 0; i < got; i++) {
        int16_t s = (int16_t)(((int32_t)mono[i] * gain) >> 15);
        frames[done + i].channel1 = s;
        frames[done + i].channel2 = s;
      }
      done += got;
    }
    if (done < count) {
      if (decodeDone) {
        playing = false;  // clip finished and drained
        portENTER_CRITICAL(&lock);
        current[0] = 0;
        portEXIT_CRITICAL(&lock);
      } else {
        underruns = underruns + 1;  // decoder fell behind: an audible dropout
      }
    }
  }

  for (int32_t i = done; i < count; i++) {
    frames[i].channel1 = 0;
    frames[i].channel2 = 0;
  }
  return count;
}

}  // namespace player
