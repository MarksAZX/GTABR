// Time-of-day lighting: keyframed sun/sky/fog/grade values interpolated by the hour (0..24).
#pragma once
#include <cmath>

#include "../core/math.h"

namespace gtabr {

struct DayLighting {
  Vec3 sunDir;        // towards the sun (or the moon at night)
  Vec3 sunColor;      // irradiance (linear HDR)
  float sunDisk = 1;  // visible sun disk intensity
  Vec3 ambSky, ambGround;
  Vec3 zenith, horizon;
  Vec3 fog;
  float fogDensity = 0.004f;
  float cloudCover = 0.45f, cloudBright = 1.0f;
  float exposure = 1.0f;
  float night = 0;    // 0 day .. 1 full night (street lights, emissive signs, stars)
  Vec3 lift, gain;
  float saturation = 1.0f, contrast = 1.0f;
  float shadowStrength = 1.0f;
};

namespace tod_detail {
struct Key {
  float hour;
  float elevDeg;
  Vec3 sun, sky, ground, zenith, horizon, fog;
  float fogD, cloudBright, exposure, night;
  Vec3 lift, gain;
  float sat, contrast;
};
// Calibrated so a mid-grey ground reads ~0.45 display at noon after ACES.
inline const Key* keys(int& n) {
  static const Key k[] = {
      // hour  elev   sun irradiance            sky ambient             ground bounce           zenith                  horizon                 fog colour              fogD    cloudB expo  night  lift                       gain                     sat   contrast
      {0.0f, 38.0f, {0.34f, 0.43f, 0.62f}, {0.075f, 0.095f, 0.17f}, {0.034f, 0.038f, 0.050f}, {0.004f, 0.007f, 0.018f}, {0.020f, 0.028f, 0.050f}, {0.030f, 0.038f, 0.060f}, 0.006f, 0.10f, 2.6f, 1.0f, {0.010f, 0.014f, 0.030f}, {0.92f, 0.96f, 1.08f}, 0.80f, 1.06f},
      {5.0f, 20.0f, {0.28f, 0.36f, 0.56f}, {0.080f, 0.100f, 0.17f}, {0.036f, 0.040f, 0.052f}, {0.010f, 0.016f, 0.040f}, {0.050f, 0.055f, 0.080f}, {0.060f, 0.065f, 0.090f}, 0.007f, 0.15f, 2.4f, 0.95f, {0.010f, 0.014f, 0.030f}, {0.94f, 0.96f, 1.06f}, 0.82f, 1.05f},
      {6.2f, 3.0f, {2.20f, 1.10f, 0.50f}, {0.30f, 0.30f, 0.40f}, {0.10f, 0.08f, 0.07f}, {0.10f, 0.16f, 0.34f}, {0.95f, 0.62f, 0.42f}, {0.70f, 0.55f, 0.48f}, 0.008f, 0.75f, 1.35f, 0.25f, {0.012f, 0.006f, 0.010f}, {1.04f, 0.98f, 0.92f}, 1.02f, 1.04f},
      {8.0f, 26.0f, {5.20f, 4.40f, 3.40f}, {0.42f, 0.52f, 0.74f}, {0.20f, 0.17f, 0.14f}, {0.16f, 0.32f, 0.72f}, {0.70f, 0.76f, 0.84f}, {0.66f, 0.72f, 0.80f}, 0.0050f, 1.00f, 1.00f, 0.0f, {0.006f, 0.004f, 0.010f}, {1.02f, 1.00f, 0.97f}, 1.04f, 1.04f},
      {12.5f, 68.0f, {6.40f, 6.00f, 5.30f}, {0.46f, 0.58f, 0.84f}, {0.24f, 0.22f, 0.19f}, {0.12f, 0.28f, 0.74f}, {0.62f, 0.72f, 0.86f}, {0.64f, 0.72f, 0.84f}, 0.0040f, 1.05f, 0.92f, 0.0f, {0.004f, 0.004f, 0.010f}, {1.00f, 1.00f, 0.99f}, 1.02f, 1.05f},
      {16.0f, 34.0f, {6.00f, 5.00f, 3.70f}, {0.42f, 0.50f, 0.70f}, {0.24f, 0.20f, 0.15f}, {0.15f, 0.30f, 0.68f}, {0.78f, 0.74f, 0.72f}, {0.74f, 0.70f, 0.66f}, 0.0045f, 1.00f, 0.95f, 0.0f, {0.010f, 0.004f, 0.000f}, {1.05f, 1.00f, 0.92f}, 1.06f, 1.06f},
      {17.8f, 7.0f, {4.20f, 2.10f, 0.90f}, {0.34f, 0.30f, 0.38f}, {0.16f, 0.11f, 0.08f}, {0.12f, 0.18f, 0.40f}, {1.10f, 0.58f, 0.32f}, {0.86f, 0.58f, 0.44f}, 0.0060f, 0.95f, 1.10f, 0.1f, {0.020f, 0.006f, 0.000f}, {1.08f, 0.96f, 0.86f}, 1.10f, 1.06f},
      {18.8f, -2.0f, {0.60f, 0.40f, 0.42f}, {0.16f, 0.14f, 0.24f}, {0.05f, 0.04f, 0.05f}, {0.03f, 0.05f, 0.14f}, {0.42f, 0.24f, 0.24f}, {0.26f, 0.20f, 0.24f}, 0.0065f, 0.45f, 1.8f, 0.6f, {0.012f, 0.008f, 0.020f}, {1.00f, 0.94f, 1.02f}, 0.92f, 1.05f},
      {20.0f, 30.0f, {0.36f, 0.45f, 0.64f}, {0.078f, 0.098f, 0.17f}, {0.034f, 0.038f, 0.050f}, {0.006f, 0.010f, 0.026f}, {0.030f, 0.036f, 0.060f}, {0.040f, 0.045f, 0.070f}, 0.0060f, 0.12f, 2.5f, 1.0f, {0.010f, 0.014f, 0.030f}, {0.92f, 0.96f, 1.08f}, 0.80f, 1.06f},
      {24.0f, 38.0f, {0.34f, 0.43f, 0.62f}, {0.075f, 0.095f, 0.17f}, {0.034f, 0.038f, 0.050f}, {0.004f, 0.007f, 0.018f}, {0.020f, 0.028f, 0.050f}, {0.030f, 0.038f, 0.060f}, 0.006f, 0.10f, 2.6f, 1.0f, {0.010f, 0.014f, 0.030f}, {0.92f, 0.96f, 1.08f}, 0.80f, 1.06f},
  };
  n = (int)(sizeof(k) / sizeof(k[0]));
  return k;
}
}  // namespace tod_detail

inline DayLighting computeDayLighting(float hour, float cloudCover) {
  using namespace tod_detail;
  hour = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
  int n;
  const Key* k = keys(n);
  int i = 0;
  while (i < n - 2 && hour >= k[i + 1].hour) ++i;
  const Key& a = k[i];
  const Key& b = k[i + 1];
  float t = clamp((hour - a.hour) / std::max(0.001f, b.hour - a.hour), 0.0f, 1.0f);
  t = t * t * (3.0f - 2.0f * t);
  auto L3 = [&](Vec3 x, Vec3 y) { return lerp(x, y, t); };
  auto L1 = [&](float x, float y) { return x + (y - x) * t; };
  DayLighting d;
  d.sunColor = L3(a.sun, b.sun);
  d.ambSky = L3(a.sky, b.sky);
  d.ambGround = L3(a.ground, b.ground);
  d.zenith = L3(a.zenith, b.zenith);
  d.horizon = L3(a.horizon, b.horizon);
  d.fog = L3(a.fog, b.fog);
  d.fogDensity = L1(a.fogD, b.fogD);
  d.cloudBright = L1(a.cloudBright, b.cloudBright);
  d.exposure = L1(a.exposure, b.exposure);
  d.night = L1(a.night, b.night);
  d.lift = L3(a.lift, b.lift);
  d.gain = L3(a.gain, b.gain);
  d.saturation = L1(a.sat, b.sat);
  d.contrast = L1(a.contrast, b.contrast);
  d.cloudCover = cloudCover;
  // sun path: rises in the east (+X), sets in the west, tilted toward the north so streets get diagonal shadows.
  // At night the key light is the moon, on a fixed high arc.
  bool moon = hour < 5.6f || hour > 18.9f;
  float elev = L1(a.elevDeg, b.elevDeg);
  float az;
  if (!moon) az = (hour - 6.0f) / 12.5f * 180.0f;  // 0 = east, 180 = west
  else az = 140.0f;
  float e = std::max(elev, 4.0f) * kDeg2Rad, z = az * kDeg2Rad;
  {
    float h = std::cos(e);
    float hx = std::cos(z), hz = -0.42f;
    float len = std::sqrt(hx * hx + hz * hz);
    d.sunDir = {hx / len * h, std::sin(e), hz / len * h};
  }
  d.sunDisk = moon ? 0.0f : clamp((elev + 1.0f) / 6.0f, 0.0f, 1.0f) * 6.0f;
  // cloudy skies soften the sun and lift the ambient
  float cc = clamp(cloudCover, 0.0f, 1.0f);
  d.sunColor = d.sunColor * (1.0f - 0.45f * cc * cc);
  d.ambSky = d.ambSky * (1.0f + 0.25f * cc);
  d.shadowStrength = moon ? 0.55f : clamp(elev / 4.0f, 0.0f, 1.0f) * (1.0f - 0.5f * cc * cc);
  return d;
}

}  // namespace gtabr
