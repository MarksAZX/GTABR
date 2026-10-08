#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
  float s = 0.0, a = 0.5;
  for (int i = 0; i < 5; ++i) { s += a * noise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
  return s;
}

float hash3(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float noise3(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), f.x), mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), f.x), mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
const float kCloudLo = 1800.0, kCloudHi = 3400.0;
float cloudDensity(vec3 p, float cov, int oct) {
  float h = clamp((p.y - kCloudLo) / (kCloudHi - kCloudLo), 0.0, 1.0);
  float shape = smoothstep(0.0, 0.12, h) * smoothstep(1.0, 0.45, h);   // flat bottoms, billowy tops
  vec3 q = p * 0.00055 + vec3(g.camPos.w * 0.004, 0.0, g.camPos.w * 0.0015);
  float n = 0.0, a = 0.55;
  for (int i = 0; i < 4; ++i) {
    if (i >= oct) break;
    n += a * noise3(q); q = q * 2.17 + vec3(3.1, 1.7, 5.3); a *= 0.5;
  }
  return clamp((n - (1.05 - cov * 0.75)) * 3.2, 0.0, 1.0) * shape;
}
// Raymarched cloud slab with one sun-ward light sample per step (Beer-Lambert + silver lining), composited over the sky.
vec4 volumetricClouds(vec3 dir) {
  float t0 = (kCloudLo - g.camPos.y) / dir.y, t1 = (kCloudHi - g.camPos.y) / dir.y;
  if (t0 > 26000.0) return vec4(0.0, 0.0, 0.0, 1.0);
  const int N = 6;
  float dt = (min(t1, 26000.0) - t0) / float(N);
  float jit = hash(dir.xz * 913.0 + g.camPos.w) ;
  vec3 sunL = g.sunDir.xyz;
  float cosSun = dot(dir, sunL);
  float phase = mix(1.0, 1.0 + 1.6 * pow(max(cosSun, 0.0), 8.0), 0.6);
  vec3 sunCol = g.sunColor.rgb * (0.9 + 0.1 * (1.0 - g.params.z));
  float T = 1.0;
  vec3 L = vec3(0.0);
  float cov = g.sky0.w;
  for (int i = 0; i < N; ++i) {
    float t = t0 + dt * (float(i) + jit);
    vec3 p = g.camPos.xyz + dir * t;
    float d = cloudDensity(p, cov, 3);
    if (d < 0.01) continue;
    float dl = cloudDensity(p + sunL * 420.0, cov, 2) + cloudDensity(p + sunL * 900.0, cov, 1) * 0.7;
    float beer = exp(-dl * 1.6);
    float powder = 1.0 - exp(-d * 4.0);
    vec3 light = sunCol * 0.34 * beer * phase * mix(0.6, 1.0, powder) + g.ambSky.rgb * 0.75 * (0.6 + 0.4 * clamp((p.y - kCloudLo) / (kCloudHi - kCloudLo), 0.0, 1.0));
    float ext = d * dt * 0.0016;
    float a = 1.0 - exp(-ext);
    L += T * a * light * g.sky1.w;
    T *= 1.0 - a;
    if (T < 0.03) break;
  }
  return vec4(L, T);
}

void main() {
  vec2 ndc = vUV * 2.0 - 1.0;
  vec4 a = g.invViewProj * vec4(ndc, 0.0, 1.0);
  vec4 b = g.invViewProj * vec4(ndc, 1.0, 1.0);
  vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
  vec3 col = skyRadiance(dir, 1.0);
  float night = g.params.z;
  // stars
  if (night > 0.01 && dir.y > 0.0) {
    vec2 sp = dir.xz / (dir.y + 0.15) * 180.0;
    float st = step(0.9975, hash(floor(sp))) * hash(floor(sp) + 3.1);
    col += vec3(0.8, 0.85, 1.0) * st * night * 0.6 * smoothstep(0.0, 0.25, dir.y);
  }
  // moon: soft disc with a halo, opposite to the sun's day arc (sunDir already points at the moon during the night)
  if (night > 0.2) {
    float sdm = dot(dir, g.sunDir.xyz);
    float disc = smoothstep(0.99935, 0.99975, sdm);
    float halo = pow(max(sdm, 0.0), 220.0) * 0.35 + pow(max(sdm, 0.0), 24.0) * 0.05;
    float craters = 0.82 + 0.18 * noise(dir.xz * 380.0 + dir.y * 220.0);
    col += vec3(0.86, 0.92, 1.0) * (disc * 3.2 * craters + halo) * night;
  }
  // clouds: raymarched volumetric slab on Ultra, a single scrolling layer projected on a plane elsewhere
  if (dir.y > 0.02 && g.post.w > 0.5) {
    vec4 cl = volumetricClouds(dir);
    float fadeH = smoothstep(0.02, 0.14, dir.y);   // thin out toward the horizon where the slab is far away
    col = mix(col, col * cl.a + cl.rgb, fadeH);
  } else if (dir.y > 0.0) {
    vec2 cp = dir.xz / (dir.y + 0.08) * 0.55 + vec2(g.camPos.w * 0.006, g.camPos.w * 0.002);
    float n = fbm(cp * 1.4);
    float cov = g.sky0.w;
    float c = smoothstep(1.0 - cov, 1.0 - cov + 0.32, n);
    float shade = 0.55 + 0.45 * smoothstep(0.3, 0.9, fbm(cp * 1.4 + g.sunDir.xz * 0.12));
    vec3 cloudCol = (g.ambSky.rgb * 0.9 + g.sunColor.rgb * 0.33 * shade) * g.sky1.w;
    float sd = max(dot(dir, g.sunDir.xyz), 0.0);
    cloudCol += g.sunColor.rgb * pow(sd, 10.0) * 0.4;
    col = mix(col, cloudCol, c * smoothstep(0.0, 0.18, dir.y) * 0.92);
  }
  outColor = vec4(col, 1.0);
}
