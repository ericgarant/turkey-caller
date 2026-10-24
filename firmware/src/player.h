// player: WAV (IMA ADPCM) decode task + ring buffer feeding the A2DP data callback.
#pragma once
#include <Arduino.h>
#include <BluetoothA2DPSource.h>

namespace player {

struct Stats {
  bool playing;
  char current[32];
  uint8_t volume;         // 0-100
  uint32_t plays;         // play requests handled
  uint32_t underruns;     // callbacks that found the buffer empty mid-clip
  uint32_t bufferedBytes; // PCM bytes waiting in the ring buffer
  uint32_t bufferSize;
  uint32_t fills;         // A2DP data callbacks so far (should rise ~350/s while connected)
  uint32_t stalls;        // clips dropped because the speaker stopped taking audio
  bool loop;              // the playing clip repeats
  uint32_t posMs;         // position in the playing clip (wraps when looping)
  uint32_t lenMs;         // length of the playing clip
};

void begin();                        // start the decode task (after LittleFS is mounted)
bool play(const char *id, bool loop = false);  // queue a clip by name ("cluck"); a new play replaces the current one
void stop();
void setVolume(uint8_t level);       // 0-100, applies now, saved to NVS later by loop()
void loop();                         // call from loop(): deferred NVS save
Stats stats();

// A2DP source data callback: fills `count` stereo frames, always returns count.
int32_t fillFrames(Frame *frames, int32_t count);

}  // namespace player
