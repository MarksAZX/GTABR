// Minimal PNG writer (stored deflate blocks) used for headless screenshots and debug dumps.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gtabr {
inline bool writePng(const std::string& path, const uint8_t* rgba, uint32_t w, uint32_t h) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[n] = c;
    }
    init = true;
  }
  auto crc = [&](const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c;
  };
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  auto be32 = [](uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; };
  auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
    uint8_t len[4];
    be32(len, (uint32_t)data.size());
    fwrite(len, 1, 4, f);
    std::vector<uint8_t> buf(4 + data.size());
    for (int i = 0; i < 4; ++i) buf[i] = (uint8_t)type[i];
    std::copy(data.begin(), data.end(), buf.begin() + 4);
    fwrite(buf.data(), 1, buf.size(), f);
    uint8_t c[4];
    be32(c, crc(buf.data(), buf.size()) ^ 0xFFFFFFFFu);
    fwrite(c, 1, 4, f);
  };
  const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr(13);
  be32(&ihdr[0], w); be32(&ihdr[4], h);
  ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  chunk("IHDR", ihdr);
  std::vector<uint8_t> raw;
  raw.reserve((size_t)(w * 4 + 1) * h);
  for (uint32_t y = 0; y < h; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba + (size_t)y * w * 4, rgba + (size_t)(y + 1) * w * 4);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  size_t pos = 0;
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  while (pos < raw.size()) {
    size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.push_back(n & 0xFF); z.push_back(n >> 8); z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  uint8_t ad[4];
  be32(ad, (b << 16) | a);
  z.insert(z.end(), ad, ad + 4);
  chunk("IDAT", z);
  chunk("IEND", {});
  fclose(f);
  return true;
}
}  // namespace gtabr
