#include "wav.h"

namespace wav {
namespace {

uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool fail(const char **why, const char *msg) {
  if (why) *why = msg;
  return false;
}

}  // namespace

bool read(File &f, Info &out, const char **why) {
  out = Info();
  uint8_t h[12];
  if (f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return fail(why, "not a WAV file");
  bool haveFmt = false, haveData = false;
  uint16_t channels = 0, bits = 0, format = 0;
  uint32_t factSamples = 0;
  const size_t fileSize = f.size();
  while (!haveData) {
    uint8_t c[8];
    if (f.read(c, 8) != 8) break;
    uint32_t size = rd32(c + 4);
    size_t body = f.position();
    if (!memcmp(c, "fmt ", 4) && size >= 16) {
      uint8_t b[16];
      if (f.read(b, 16) != 16) return fail(why, "truncated fmt chunk");
      format = rd16(b);
      channels = rd16(b + 2);
      out.rate = rd32(b + 4);
      out.blockAlign = rd16(b + 12);
      bits = rd16(b + 14);
      haveFmt = true;
    } else if (!memcmp(c, "fact", 4) && size >= 4) {
      uint8_t b[4];
      if (f.read(b, 4) != 4) return fail(why, "truncated fact chunk");
      factSamples = rd32(b);
    } else if (!memcmp(c, "data", 4)) {
      out.dataStart = body;
      out.dataBytes = size;
      if (body + size > fileSize) out.dataBytes = fileSize - body;  // truncated file: play what is there
      haveData = true;
      break;
    }
    if (body + size + (size & 1) > fileSize) break;
    f.seek(body + size + (size & 1));
  }
  if (!haveFmt) return fail(why, "no fmt chunk");
  if (!haveData || out.dataBytes == 0) return fail(why, "no audio data");
  if (channels != 1) return fail(why, "not mono");
  if (out.rate < 8000 || out.rate > kMaxRate) return fail(why, "sample rate not 8-44.1 kHz");
  out.adpcm = format == 0x11;
  if (out.adpcm) {
    if (bits != 4 || out.blockAlign <= 4 || out.blockAlign > kMaxBlock) return fail(why, "unsupported ADPCM block size");
    uint32_t perBlock = (out.blockAlign - 4) * 2 + 1;
    uint32_t full = out.dataBytes / out.blockAlign, rest = out.dataBytes % out.blockAlign;
    uint32_t computed = full * perBlock + (rest > 4 ? (rest - 4) * 2 + 1 : 0);
    out.samples = factSamples && factSamples <= computed ? factSamples : computed;
  } else if (format == 1 && bits == 16) {
    out.samples = out.dataBytes / 2;
  } else {
    return fail(why, "not IMA ADPCM or 16-bit PCM");
  }
  f.seek(out.dataStart);
  return true;
}

}  // namespace wav
