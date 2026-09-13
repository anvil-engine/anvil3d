#include "formats/wav.h"
#include "check.h"

#include <cstdint>
#include <string>

using namespace anvil;

namespace {
void put16(std::string& s, uint16_t v) { s.push_back(char(v)); s.push_back(char(v >> 8)); }
void put32(std::string& s, uint32_t v) { put16(s, uint16_t(v)); put16(s, uint16_t(v >> 16)); }

std::string pcm16() {
  std::string s = "RIFF"; put32(s, 40); s += "WAVEfmt "; put32(s, 16); put16(s, 1); put16(s, 1);
  put32(s, 8000); put32(s, 16000); put16(s, 2); put16(s, 16); s += "data"; put32(s, 4);
  put16(s, 0); put16(s, 0x7fff); return s;
}
} // namespace

int main() {
  std::string error;
  auto audio = wav::decode(pcm16(), &error);
  CHECK(audio && audio->sampleRate == 8000 && audio->channels == 1 && audio->samples.size() == 2);
  CHECK(audio && audio->samples[0] == 0 && audio->samples[1] > 0.99f);
  std::string truncated = pcm16(); truncated.pop_back();
  CHECK(!wav::decode(truncated, &error));
  std::string compressed = pcm16(); compressed[20] = 2;
  CHECK(!wav::decode(compressed, &error));
  return TEST_RESULT();
}
