// Small utilities: RNG, object pool, ring helpers, string helpers.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "math.h"

namespace gtabr {

// Deterministic xorshift RNG (so the generated neighbourhood and tests are reproducible).
class Rng {
 public:
  explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s_(seed ? seed : 1) {}
  uint32_t next() {
    s_ ^= s_ << 13; s_ ^= s_ >> 7; s_ ^= s_ << 17;
    return (uint32_t)(s_ >> 16);
  }
  float uni() { return (next() & 0xFFFFFF) / 16777216.0f; }
  float range(float a, float b) { return a + (b - a) * uni(); }
  int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
  bool chance(float p) { return uni() < p; }
 private:
  uint64_t s_;
};

// Fixed-capacity object pool with stable indices; no allocation after construction.
template <class T>
class Pool {
 public:
  explicit Pool(size_t cap = 0) { reserve(cap); }
  void reserve(size_t cap) {
    items_.resize(cap);
    alive_.assign(cap, 0);
    free_.clear();
    for (size_t i = cap; i > 0; --i) free_.push_back((int)(i - 1));
  }
  // Returns nullptr when exhausted (callers drop the effect: pooled objects are cosmetic).
  T* acquire(int* outIndex = nullptr) {
    if (free_.empty()) return nullptr;
    int i = free_.back();
    free_.pop_back();
    alive_[i] = 1;
    items_[i] = T{};
    if (outIndex) *outIndex = i;
    return &items_[i];
  }
  void release(int i) {
    if (i >= 0 && alive_[i]) { alive_[i] = 0; free_.push_back(i); }
  }
  template <class F>
  void forEach(F&& f) {
    for (size_t i = 0; i < items_.size(); ++i)
      if (alive_[i]) f(items_[i], (int)i);
  }
  size_t capacity() const { return items_.size(); }
  size_t aliveCount() const { return items_.size() - free_.size(); }
 private:
  std::vector<T> items_;
  std::vector<uint8_t> alive_;
  std::vector<int> free_;
};

inline std::string fmtMoney(int cents) {
  // R$ 1.234,56 (pt-BR)
  bool neg = cents < 0;
  if (neg) cents = -cents;
  int reais = cents / 100, c = cents % 100;
  std::string r = std::to_string(reais), out;
  int cnt = 0;
  for (int i = (int)r.size() - 1; i >= 0; --i) {
    out.insert(out.begin(), r[i]);
    if (++cnt % 3 == 0 && i > 0) out.insert(out.begin(), '.');
  }
  char buf[16];
  snprintf(buf, sizeof(buf), ",%02d", c);
  return std::string(neg ? "-R$ " : "R$ ") + out + buf;
}

inline std::string fmtFloat(float v, int decimals = 1) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.*f", decimals, v);
  for (char& c : buf) if (c == '.') c = ',';
  return buf;
}

}  // namespace gtabr
