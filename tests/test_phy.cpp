#include "formats/phy.h"
#include "filesystem/vpk.h"
#include "check.h"

#include <cstring>
#include <string>

namespace {
template <class T> void put(std::string& bytes, size_t at, T value) {
  if (bytes.size() < at + sizeof(T)) bytes.resize(at + sizeof(T));
  std::memcpy(bytes.data() + at, &value, sizeof(T));
}
std::string tetrahedron() {
  std::string b(16 + 4 + 16 + 64 + 32 + 16 + 4 * 16 + 4 * 16, '\0');
  put<int32_t>(b, 0, 16); put<int32_t>(b, 8, 1); put<uint32_t>(b, 12, 123);
  const size_t surface = 16, compact = 32, node = 96, ledge = 128, points = 208;
  put<int32_t>(b, surface, int32_t(b.size() - surface - 4));
  put<int16_t>(b, surface + 8, 0x100); put<int16_t>(b, surface + 10, 0);
  put<int32_t>(b, surface + 12, int32_t(b.size() - compact));
  put<int32_t>(b, compact + 48, int32_t(node - (compact + 16)));
  std::memcpy(b.data() + compact + 60, "IVPS", 4);
  put<int32_t>(b, node + 4, int32_t(ledge - node));
  put<int32_t>(b, ledge, int32_t(points - ledge)); put<uint16_t>(b, ledge + 12, 4);
  const uint16_t triangles[4][3] = {{0,2,1},{0,1,3},{1,2,3},{2,0,3}};
  for (size_t t=0;t<4;++t) for(size_t e=0;e<3;++e) put<uint32_t>(b,ledge+20+t*16+e*4,triangles[t][e]);
  const float vertices[4][3]={{0,0,0},{16,0,0},{0,16,0},{0,0,16}};
  for(size_t i=0;i<4;++i) std::memcpy(b.data()+points+i*16,vertices[i],12);
  return b;
}
}

int main(int argc, char** argv) {
  if (argc > 1) {
    size_t files=0, parsed=0, hulls=0, masses=0, unsupported=0, bad=0;
    for (int arg=1; arg<argc; ++arg) {
      std::string openError;
      auto archive=anvil::VpkArchive::open(argv[arg],&openError);
      CHECK(archive); if(!archive) continue;
      for (const auto& path:archive->files()) {
        if (!path.ends_with(".phy")) continue;
        ++files;
        const auto bytes=archive->read(path);
        std::string parseError;
        const auto model=bytes?anvil::phy::load(*bytes,&parseError):std::nullopt;
        if (model) { ++parsed; hulls+=model->hulls.size(); masses+=model->mass>0; }
        else if (parseError=="unsupported PHY surface type" || parseError=="degenerate PHY hull") ++unsupported;
        else ++bad;
      }
    }
    std::printf("%zu PHY files, %zu compact parsed, %zu hulls, %zu masses, %zu unsupported, %zu bad\n",files,parsed,hulls,masses,unsupported,bad);
    CHECK(files>0 && parsed>0 && masses>0 && bad==0);
    return TEST_RESULT();
  }
  std::string error;
  const std::string good = tetrahedron();
  auto model = anvil::phy::load(good, &error);
  CHECK(model && model->checksum == 123 && model->hulls.size() == 1 && model->hulls[0].size() == 4);
  for (size_t cut=0; cut<good.size(); ++cut) CHECK(!anvil::phy::load(std::string_view(good).substr(0,cut), &error));
  std::string bad=good; put<int16_t>(bad,26,2); CHECK(!anvil::phy::load(bad,&error));
  bad=good; put<int32_t>(bad,80,INT32_MAX); CHECK(!anvil::phy::load(bad,&error));
  return TEST_RESULT();
}
