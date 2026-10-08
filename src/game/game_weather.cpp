// Weather: clear / overcast / rain / storm with smooth transitions. Rain darkens and greys the lighting, wets the ground (puddles in
// the world shader), thickens the fog, gusts the wind (foliage sway), streaks the air with falling drops and, in storms, lightning
// with delayed thunder. The sequence is automatic (random but seeded by the city) or forced from the pause menu.
#include <cmath>

#include "../core/log.h"
#include "game.h"

namespace gtabr {

namespace {
float hash1(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return (x & 0xFFFFFF) / 16777215.0f;
}
}  // namespace

void Game::setWeatherMode(int mode) {
  weatherMode_ = clamp(mode, 0, 3);
  weatherTimer_ = 0;   // re-pick immediately
  if (weatherMode_ == 1) weatherTarget_ = 0.0f;
  else if (weatherMode_ == 2) weatherTarget_ = 0.6f;
  else if (weatherMode_ == 3) weatherTarget_ = 1.0f;
}

void Game::updateWeather(float dt) {
  weatherTimer_ -= dt;
  if (weatherMode_ == 0 && weatherTimer_ <= 0) {
    // automatic: long dry spells, now and then a shower or a storm
    float r = wRng_.uni();
    if (weatherTarget_ > 0.05f) { weatherTarget_ = 0.0f; weatherTimer_ = wRng_.range(160.0f, 340.0f); }
    else if (r < 0.40f) { weatherTarget_ = wRng_.chance(0.3f) ? 1.0f : wRng_.range(0.4f, 0.75f); weatherTimer_ = wRng_.range(70.0f, 170.0f); }
    else weatherTimer_ = wRng_.range(120.0f, 260.0f);
  } else if (weatherMode_ != 0) weatherTimer_ = 1e9f;
  rain_ += (weatherTarget_ - rain_) * expDecay(0.22f, dt);
  if (std::fabs(rain_ - weatherTarget_) < 0.002f) rain_ = weatherTarget_;
  // ground wets quickly and dries slowly
  float wetTarget = clamp(rain_ * 1.6f, 0.0f, 1.0f);
  wetness_ += (wetTarget - wetness_) * expDecay(wetTarget > wetness_ ? 0.18f : 0.012f, dt);
  // wind: breezy baseline, stronger with weather, slow gusts
  float gust = 0.5f + 0.5f * std::sin(time_ * 0.37f) * std::sin(time_ * 0.11f + 1.0f);
  wind_ = clamp(0.25f + 0.35f * rain_ + 0.25f * gust, 0.0f, 1.0f);
  // lightning in storms
  flash_ = std::max(0.0f, flash_ - dt * 3.2f);
  if (rain_ > 0.75f && !player_.indoors) {
    flashTimer_ -= dt;
    if (flashTimer_ <= 0) {
      flash_ = 1.0f;
      flashTimer_ = wRng_.range(6.0f, 18.0f);
      thunderIn_ = wRng_.range(0.4f, 3.0f);
    }
  }
  if (thunderIn_ > 0) {
    thunderIn_ -= dt;
    if (thunderIn_ <= 0) audio_.play("thunder", {player_.pos.x, 10.0f, player_.pos.y}, 0.9f);
  }
  // ambient rain loop (not heard indoors)
  float vol = player_.indoors ? 0.0f : clamp(rain_, 0.0f, 1.0f) * 0.5f;
  Vec3 at{player_.pos.x, 2.0f, player_.pos.y};
  if (vol > 0.02f && !rainHandle_) rainHandle_ = audio_.loopStart("rain", at, vol);
  else if (rainHandle_ && vol <= 0.02f) { audio_.loopStop(rainHandle_); rainHandle_ = 0; }
  else if (rainHandle_) audio_.loopUpdate(rainHandle_, at, vol);
}

// Overcast, grey, gloomy light as the rain grows (applied on top of the time-of-day keyframes).
void Game::applyWeatherToLighting(DayLighting& d) const {
  float r = rain_;
  if (r > 0.001f) {
    float lum = 0.3f * d.horizon.x + 0.59f * d.horizon.y + 0.11f * d.horizon.z;
    Vec3 grey = Vec3{0.92f, 0.96f, 1.0f} * lum * 0.7f;
    d.sunColor = d.sunColor * (1.0f - 0.78f * r);
    d.sunDisk *= 1.0f - r;
    d.ambSky = lerp(d.ambSky, Vec3{d.ambSky.y, d.ambSky.y, d.ambSky.z} * 0.9f, r * 0.6f);
    d.horizon = lerp(d.horizon, grey, r * 0.85f);
    d.zenith = lerp(d.zenith, grey * 0.55f, r * 0.85f);
    d.fog = lerp(d.fog, grey, r * 0.7f);
    d.fogDensity *= 1.0f + 2.2f * r;
    d.cloudCover = lerp(d.cloudCover, 1.0f, r);
    d.cloudBright *= 1.0f - 0.45f * r;
    d.saturation *= 1.0f - 0.22f * r;
    d.shadowStrength *= 1.0f - 0.85f * r;
    d.exposure *= 1.0f + 0.12f * r;
  }
  if (flash_ > 0.01f) {
    float f = flash_ * flash_ * (0.6f + 0.4f * std::sin(flash_ * 40.0f)) * (settings_.reduceFlashes ? 0.25f : 1.0f);
    d.ambSky = d.ambSky + Vec3{0.9f, 1.0f, 1.4f} * f * 1.2f;
    d.zenith = d.zenith + Vec3{0.5f, 0.55f, 0.8f} * f;
    d.horizon = d.horizon + Vec3{0.6f, 0.65f, 0.85f} * f;
  }
}

// Falling drops are billboard streaks in a box around the camera focus: the pattern is world anchored (a hash lattice wrapped
// around the focus), so it costs no pool or per-drop state and never "pops".
void Game::emitRain() {
  if (rain_ < 0.03f || player_.indoors || !settings_.weatherFx) return;
  UvRect dot = assets_.icon("dot");
  if (!dot.valid) return;
  SpriteDef sd;
  sd.tex = assets_.iconsTex; sd.u0 = dot.u0; sd.v0 = dot.v0; sd.u1 = dot.u1; sd.v1 = dot.v1;
  sd.pivX = 0.5f; sd.pivY = 0.5f; sd.valid = true;
  const Vec3 f = cam_.focus();
  const float B = 34.0f, H = 16.0f;
  int n = (int)(rain_ * (preset().maxLights >= 12 ? 520.0f : 260.0f));
  const float speed = 15.0f;
  auto wrap = [](float v, float L) { return v - L * std::floor(v / L + 0.5f); };
  for (int i = 0; i < n; ++i) {
    float hx = hash1(i * 3 + 1) * B, hz = hash1(i * 3 + 2) * B, hy = hash1(i * 3 + 3) * H;
    float x = f.x + wrap(hx - f.x, B), z = f.z + wrap(hz - f.z, B);
    float y = H - std::fmod(hy + time_ * speed * (0.85f + 0.3f * hash1(i * 7)), H);
    x += wind_ * y * 0.12f;
    float len = 0.55f + 0.5f * hash1(i * 5);
    sd.wm = 0.018f; sd.hm = len;
    addSprite(&sd, {x, y, z}, 1.0f, 0.18f + 0.1f * rain_, false, packRGBA8(0.75f, 0.82f, 0.95f, 1.0f), false, false, 0.5f);
    if (y < 0.5f && (i % 3) == 0) {   // splash where the drop lands
      SpriteDef sp = sd;
      sp.wm = 0.18f; sp.hm = 0.06f;
      addSprite(&sp, {x, 0.16f, z}, 1.0f, 0.35f, false, packRGBA8(0.85f, 0.9f, 1.0f, 1.0f), false, false, 0.2f);
    }
  }
}

}  // namespace gtabr
