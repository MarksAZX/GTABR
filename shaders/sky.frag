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
  for (int i = 0; i < (g.weather.w<=1.0?3:5); ++i) { s += a * noise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
  return s;
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
  // clouds: a single scrolling layer projected on a plane
  if (dir.y > 0.0) {
    vec2 cp = dir.xz / (dir.y + 0.08) * 0.55 + vec2(g.camPos.w * 0.006, g.camPos.w * 0.002);
    float n = fbm(cp * 1.4);
    float cov = g.sky0.w;
    float detail=fbm(cp*3.7+vec2(3.7,-1.2));
    float c = smoothstep(1.0 - cov, 1.0 - cov + 0.28, n*0.82+detail*0.18);
    float shade = 0.55 + 0.45 * smoothstep(0.3, 0.9, fbm(cp * 1.4 + g.sunDir.xz * 0.12));
    vec3 cloudCol = (g.ambSky.rgb * 0.9 + g.sunColor.rgb * 0.33 * shade) * g.sky1.w;
    float sd = max(dot(dir, g.sunDir.xyz), 0.0);
    cloudCol += g.sunColor.rgb * pow(sd, 10.0) * 0.4;
    cloudCol*=1-g.weather.x*0.3;
    col = mix(col, cloudCol, c * smoothstep(0.0, 0.18, dir.y) * 0.92);
  }
  outColor = vec4(col, 1.0);
}
