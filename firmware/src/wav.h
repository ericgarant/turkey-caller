// wav: reads the header of a clip file (mono IMA ADPCM or 16-bit PCM WAV).
#pragma once
#include <Arduino.h>
#include <FS.h>

namespace wav {

struct Info {
  bool adpcm = false;
  uint32_t rate = 0;
  uint16_t blockAlign = 0;   // ADPCM: bytes per block (4-byte header + nibbles)
  uint32_t dataStart = 0;    // file offset of the first audio byte
  uint32_t dataBytes = 0;    // size of the data chunk
  uint32_t samples = 0;      // total samples (from the fact chunk, else computed)
  uint32_t ms() const { return rate ? (uint32_t)((uint64_t)samples * 1000 / rate) : 0; }
};

constexpr size_t kMaxBlock = 1024;  // largest ADPCM block the player reads at once (ffmpeg writes 1024)
constexpr uint32_t kMaxRate = 44100;

// Reads the header and leaves `f` at the first audio byte. On a file the player can't play it
// returns false and, when `why` is given, a short reason.
bool read(File &f, Info &out, const char **why = nullptr);

}  // namespace wav
