#include "audio.h"

#include <algorithm>
#include <cmath>

#include "log.h"
#include "util.h"

#if defined(__ANDROID__)
#include <aaudio/AAudio.h>
#endif

namespace gtabr {

namespace {
struct Synth {
  int rate;
  uint32_t seed = 0x1234567u;
  float noise() { seed = seed * 1664525u + 1013904223u; return ((seed >> 9) & 0x7FFFFF) / 4194303.5f - 1.0f; }
  std::vector<float> buf(float secs) { return std::vector<float>((size_t)(secs * rate), 0.0f); }
};
// one-pole low/high pass helpers
struct LP { float a, z = 0; LP(float cutoff, int rate) : a(1.0f - std::exp(-2.0f * kPi * cutoff / rate)) {} float operator()(float x) { z += a * (x - z); return z; } };

std::vector<float> gunshot(Synth& s, float len, float body, float crack, float tail, float lpHz) {
  auto b = s.buf(len);
  LP lp(lpHz, s.rate), lpTail(900.0f, s.rate);
  float ph = 0;
  for (size_t i = 0; i < b.size(); ++i) {
    float t = (float)i / s.rate;
    float n = s.noise();
    float crackEnv = std::exp(-t * 90.0f) * crack;
    float boomEnv = std::exp(-t * 18.0f) * body;
    ph += 2.0f * kPi * (55.0f + 40.0f * std::exp(-t * 30.0f)) / s.rate;
    float tailEnv = std::exp(-t * (4.0f / tail)) * 0.25f;
    b[i] = lp(n) * crackEnv + std::sin(ph) * boomEnv + lpTail(n) * tailEnv;
  }
  return b;
}
std::vector<float> thud(Synth& s, float len, float freq, float noiseAmt, float decay) {
  auto b = s.buf(len);
  LP lp(1400.0f, s.rate);
  float ph = 0;
  for (size_t i = 0; i < b.size(); ++i) {
    float t = (float)i / s.rate;
    ph += 2.0f * kPi * freq * (1.0f + 1.5f * std::exp(-t * 40.0f)) / s.rate;
    float env = std::exp(-t * decay);
    b[i] = (std::sin(ph) * 0.8f + lp(s.noise()) * noiseAmt) * env;
  }
  return b;
}
std::vector<float> whoosh(Synth& s, float len, float hz) {
  auto b = s.buf(len);
  LP lp(hz, s.rate);
  for (size_t i = 0; i < b.size(); ++i) {
    float t = (float)i / b.size();
    float env = std::sin(t * kPi);
    b[i] = lp(s.noise()) * env * env * 0.7f;
  }
  return b;
}
// two quick bright pings: notes being collected
std::vector<float> coinPing(Synth& s) {
  auto b = s.buf(0.34f);
  for (size_t i = 0; i < b.size(); ++i) {
    float t = (float)i / s.rate;
    float f = t < 0.09f ? 1568.0f : 2093.0f;
    float tt = t < 0.09f ? t : t - 0.09f;
    b[i] = (std::sin(2.0f * kPi * f * t) + 0.4f * std::sin(2.0f * kPi * f * 2.0f * t)) * std::exp(-tt * 16.0f) * 0.35f;
  }
  return b;
}

std::vector<float> clank(Synth& s, float len, float f0) {
  auto b = s.buf(len);
  const float partials[4] = {1.0f, 2.76f, 5.4f, 8.93f};
  for (size_t i = 0; i < b.size(); ++i) {
    float t = (float)i / s.rate;
    float v = 0;
    for (int k = 0; k < 4; ++k) v += std::sin(2.0f * kPi * f0 * partials[k] * t) * std::exp(-t * (14.0f + k * 9.0f)) / (k + 1);
    b[i] = v * 0.6f + s.noise() * std::exp(-t * 120.0f) * 0.5f;
  }
  return b;
}
std::vector<float> click(Synth& s, float len, float f) {
  auto b = s.buf(len);
  for (size_t i = 0; i < b.size(); ++i) {
    float t = (float)i / s.rate;
    b[i] = (s.noise() * 0.6f + std::sin(2 * kPi * f * t) * 0.4f) * std::exp(-t * 160.0f);
  }
  return b;
}
std::vector<float> siren(Synth& s) {
  // two-tone wail (rising/falling), 2 s loop
  auto b = s.buf(2.0f);
  float ph = 0;
  for (size_t i = 0; i < b.size(); ++i) {
    float t = (float)i / s.rate;
    float f = 720.0f + 380.0f * (0.5f - 0.5f * std::cos(t / 2.0f * 2.0f * kPi));
    ph += 2.0f * kPi * f / s.rate;
    float sq = std::sin(ph) + 0.33f * std::sin(3 * ph) + 0.2f * std::sin(5 * ph);
    b[i] = sq * 0.32f;
  }
  return b;
}
std::vector<float> surf(Synth& s) {
  // breaking waves: low-passed noise swelling every ~3 s (6 s loop, two waves)
  auto b = s.buf(6.0f);
  float lp = 0, lp2 = 0;
  for (size_t i = 0; i < b.size(); ++i) {
    float t = (float)i / s.rate;
    float ph = std::fmod(t, 3.0f) / 3.0f;
    float env = 0.25f + 0.75f * std::pow(std::sin(ph * kPi), 3.0f) * std::exp(-ph * 1.2f);
    lp += (s.noise() - lp) * 0.08f;
    lp2 += (lp - lp2) * 0.15f;
    b[i] = lp2 * env * 2.2f;
  }
  // fade the loop seam
  size_t f = (size_t)(s.rate * 0.05f);
  for (size_t i = 0; i < f; ++i) { float k = (float)i / f; b[i] *= k; b[b.size() - 1 - i] *= k; }
  return b;
}
std::vector<float> horn(Synth& s) {
  // two-tone car horn (a minor third apart) with a slightly buzzy edge
  auto b = s.buf(0.55f);
  for (size_t i = 0; i < b.size(); ++i) {
    float t = (float)i / s.rate;
    float env = std::min(1.0f, t * 60.0f) * std::min(1.0f, (0.55f - t) * 18.0f);
    float v = std::sin(2 * kPi * 415.0f * t) + 0.35f * std::sin(2 * kPi * 830.0f * t) + std::sin(2 * kPi * 494.0f * t) + 0.3f * std::sin(2 * kPi * 988.0f * t);
    b[i] = std::tanh(v * 0.9f) * 0.28f * env;
  }
  return b;
}
std::vector<float> rainLoop(Synth& s) {
  // steady hiss: high-passed noise with slow variation (4 s loop)
  auto b = s.buf(4.0f);
  float lp = 0, prev = 0;
  for (size_t i = 0; i < b.size(); ++i) {
    float t = (float)i / s.rate;
    float n = s.noise();
    lp += (n - lp) * 0.55f;
    float hp = lp - prev * 0.6f;
    prev = lp;
    b[i] = hp * (0.5f + 0.15f * std::sin(t * 2.1f) + 0.1f * std::sin(t * 5.3f));
  }
  size_t f = (size_t)(s.rate * 0.05f);
  for (size_t i = 0; i < f; ++i) { float k = (float)i / f; b[i] *= k; b[b.size() - 1 - i] *= k; }
  return b;
}
std::vector<float> thunder(Synth& s) {
  auto b = s.buf(3.4f);
  float lp = 0, lp2 = 0;
  for (size_t i = 0; i < b.size(); ++i) {
    float t = (float)i / s.rate;
    lp += (s.noise() - lp) * 0.02f;
    lp2 += (lp - lp2) * 0.05f;
    float env = (1.0f - std::exp(-t * 14.0f)) * std::exp(-t * 1.1f) * (0.65f + 0.35f * std::sin(t * 9.0f + std::sin(t * 3.0f) * 2.0f));
    b[i] = lp2 * env * 14.0f;
  }
  return b;
}
}  // namespace

void Audio::synthesize() {
  Synth s{rate_};
  samples_["pistol"].data = gunshot(s, 0.55f, 0.9f, 1.0f, 0.5f, 5200.0f);
  samples_["revolver"].data = gunshot(s, 0.8f, 1.2f, 1.0f, 0.8f, 3800.0f);
  samples_["smg"].data = gunshot(s, 0.28f, 0.6f, 0.9f, 0.2f, 6500.0f);
  samples_["shotgun"].data = gunshot(s, 1.0f, 1.5f, 1.0f, 1.0f, 2600.0f);
  samples_["punch"].data = thud(s, 0.22f, 95.0f, 0.5f, 26.0f);
  samples_["blunt"].data = thud(s, 0.3f, 70.0f, 0.7f, 18.0f);
  samples_["slash"].data = whoosh(s, 0.22f, 4200.0f);
  samples_["cash"].data = coinPing(s);
  samples_["metal"].data = clank(s, 0.6f, 520.0f);
  samples_["swing"].data = whoosh(s, 0.3f, 1800.0f);
  samples_["reload"].data = click(s, 0.12f, 1800.0f);
  samples_["empty"].data = click(s, 0.06f, 2600.0f);
  samples_["crash"].data = thud(s, 0.7f, 48.0f, 1.0f, 7.0f);
  samples_["hurt"].data = thud(s, 0.18f, 160.0f, 0.3f, 30.0f);
  samples_["body"].data = thud(s, 0.4f, 60.0f, 0.6f, 12.0f);
  samples_["siren"].data = siren(s);
  samples_["siren"].loop = true;
  samples_["splash"].data = whoosh(s, 0.55f, 900.0f);
  samples_["surf"].data = surf(s);
  samples_["surf"].loop = true;
  samples_["rain"].data = rainLoop(s);
  samples_["rain"].loop = true;
  samples_["thunder"].data = thunder(s);
  samples_["horn"].data = horn(s);
}

bool Audio::init() {
  synthesize();
  voices_.resize(24);
#if defined(__ANDROID__)
  AAudioStreamBuilder* b = nullptr;
  if (AAudio_createStreamBuilder(&b) == AAUDIO_OK) {
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, rate_);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(b, [](AAudioStream*, void* user, void* data, int32_t frames) -> aaudio_data_callback_result_t {
      static_cast<Audio*>(user)->mix(static_cast<float*>(data), frames);
      return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }, this);
    AAudioStream* st = nullptr;
    if (AAudioStreamBuilder_openStream(b, &st) == AAUDIO_OK) {
      rate_ = AAudioStream_getSampleRate(st);
      if (AAudioStream_requestStart(st) == AAUDIO_OK) device_ = st;
      else AAudioStream_close(st);
    }
    AAudioStreamBuilder_delete(b);
  }
  LOGI("Audio: %s", device_ ? "AAudio stream running" : "no output device");
#else
  LOGI("Audio: mixer without output device (desktop/headless)");
#endif
  return true;
}

void Audio::shutdown() {
#if defined(__ANDROID__)
  if (device_) {
    AAudioStream* st = static_cast<AAudioStream*>(device_);
    AAudioStream_requestStop(st);
    AAudioStream_close(st);
    device_ = nullptr;
  }
#endif
}

void Audio::play(const std::string& id, Vec3 pos, float vol, float pitch) {
  std::lock_guard<std::mutex> l(m_);
  counts_[id]++;
  auto it = samples_.find(id);
  if (it == samples_.end()) return;
  // cull inaudible sounds
  if ((pos - listener_).length() > 120.0f) return;
  Voice* slot = nullptr;
  for (Voice& v : voices_) if (!v.active) { slot = &v; break; }
  if (!slot) {  // steal the quietest one-shot
    for (Voice& v : voices_) if (!v.loop && (!slot || v.vol < slot->vol)) slot = &v;
    if (!slot) return;
  }
  *slot = Voice{&it->second, 0.0, pitch, vol, pos, false, 0, true};
}

int Audio::loopStart(const std::string& id, Vec3 pos, float vol) {
  std::lock_guard<std::mutex> l(m_);
  counts_[id]++;
  auto it = samples_.find(id);
  if (it == samples_.end()) return 0;
  for (Voice& v : voices_)
    if (!v.active) {
      v = Voice{&it->second, 0.0, 1.0f, vol, pos, true, nextHandle_++, true};
      return v.handle;
    }
  return 0;
}

void Audio::loopUpdate(int handle, Vec3 pos, float vol, float pitch) {
  std::lock_guard<std::mutex> l(m_);
  for (Voice& v : voices_)
    if (v.active && v.handle == handle) { v.p = pos; v.vol = vol; v.pitch = pitch; }
}

void Audio::loopStop(int handle) {
  std::lock_guard<std::mutex> l(m_);
  for (Voice& v : voices_)
    if (v.active && v.handle == handle) v.active = false;
}

int Audio::playedCount(const std::string& id) const {
  std::lock_guard<std::mutex> l(m_);
  auto it = counts_.find(id);
  return it == counts_.end() ? 0 : it->second;
}

void Audio::mix(float* out, int frames) {
  std::fill(out, out + frames * 2, 0.0f);
  std::lock_guard<std::mutex> l(m_);
  float master = master_;
  const float sfxVol = sfx_, ambVol = amb_;
  for (Voice& v : voices_) {
    if (!v.active || !v.s || v.s->data.empty()) continue;
    Vec3 d = v.p - listener_;
    float dist = d.length();
    float att = v.vol * (v.loop ? ambVol : sfxVol) / (1.0f + dist * dist * 0.012f);
    float pan = dist > 0.5f ? clamp(d.dot(listenerRight_) / dist, -1.0f, 1.0f) * 0.7f : 0.0f;
    float gl = att * std::sqrt(0.5f * (1.0f - pan)), gr = att * std::sqrt(0.5f * (1.0f + pan));
    const std::vector<float>& s = v.s->data;
    double step = v.pitch;
    for (int i = 0; i < frames; ++i) {
      size_t i0 = (size_t)v.pos;
      if (i0 + 1 >= s.size()) {
        if (v.loop) { v.pos = 0; i0 = 0; }
        else { v.active = false; break; }
      }
      float f = (float)(v.pos - i0);
      float x = s[i0] + (s[i0 + 1] - s[i0]) * f;
      out[i * 2] += x * gl;
      out[i * 2 + 1] += x * gr;
      v.pos += step;
    }
  }
  for (int i = 0; i < frames * 2; ++i) out[i] = std::tanh(out[i] * master);   // soft limiter
}

}  // namespace gtabr
