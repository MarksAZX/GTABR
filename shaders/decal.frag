#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(set = 0, binding = 1) uniform sampler2DArrayShadow uShadow;
#include "shadowing.glsl"
#include "probes.glsl"
layout(location = 0) in vec2 vP;
layout(location = 1) in vec2 vAK;
layout(location = 2) in vec3 vWorld;
layout(location = 3) in vec3 vSeed;
layout(location = 4) in vec3 vNormal;
layout(location = 0) out vec4 outColor;

float sdRoundBox(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float h21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(h21(i), h21(i + vec2(1, 0)), f.x), mix(h21(i + vec2(0, 1)), h21(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
  float a = 0.5, s = 0.0;
  for (int i = 0; i < 4; ++i) { s += a * vnoise(p); p = p * 2.03 + 11.7; a *= 0.5; }
  return s;
}
float segDist(vec2 p, vec2 a, vec2 b) {
  vec2 pa = p - a, ba = b - a;
  return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0));
}

// Real surface decals (cracks, oil, manholes, drains, tyre marks, graffiti, leaves, patches, wet spots). They are shaded
// like the ground under them: sun + cascaded shadow, sky ambient and the dynamic lights, so they sit in the scene at
// every time of day. kind 0 / 1 stay the soft blob shadows.
void main() {
  float kind = vAK.y;
  if (kind < 1.5) {
    float m;
    if (kind < 0.5) {
      m = 1.0 - smoothstep(0.1, 1.0, length(vP));
      m *= m;
    } else {
      float d = sdRoundBox(vP, vec2(0.86), 0.34);
      m = 1.0 - smoothstep(-0.55, 0.12, d);
    }
    outColor = vec4(0.02, 0.025, 0.04, m * vAK.x);
    return;
  }
  vec2 p = vP;
  vec2 seed = vSeed.xy;
  float scale = vSeed.z;                 // metres per half extent
  bool vertical = kind >= 19.5;
  float k = floor(kind + 0.5) - (vertical ? 20.0 : 0.0);
  vec3 albedo = vec3(0.1);
  float a = 0.0;
  float rough = 0.8, sheen = 0.0;
  float edge = 1.0 - smoothstep(0.82, 1.0, max(abs(p.x), abs(p.y)));   // keep the quad borders invisible
  if (k < 2.5) {                                     // 2: crack network
    vec2 q = p * 2.2 + seed * 40.0;
    float n = fbm(q);
    float line = 1.0 - smoothstep(0.0, 0.035, abs(n - 0.5));
    float n2 = fbm(q * 2.7 + 5.0);
    float fine = 1.0 - smoothstep(0.0, 0.02, abs(n2 - 0.5));
    a = max(line, fine * 0.7) * smoothstep(1.0, 0.25, length(p)) * 0.85;
    albedo = vec3(0.035);
  } else if (k < 3.5) {                              // 3: oil / fluid stain
    float r = length(p * vec2(1.0, 0.9)) + (fbm(p * 3.0 + seed * 20.0) - 0.5) * 0.55;
    a = (1.0 - smoothstep(0.35, 0.85, r)) * 0.8;
    albedo = vec3(0.02, 0.018, 0.016);
    rough = 0.22;
    sheen = 0.5 * (1.0 - smoothstep(0.1, 0.7, r));
  } else if (k < 4.5) {                              // 4: manhole cover
    float r = length(p);
    float disc = 1.0 - smoothstep(0.82, 0.86, r);
    float rim = smoothstep(0.66, 0.70, r) * (1.0 - smoothstep(0.80, 0.84, r));
    float ang = atan(p.y, p.x);
    float spokes = smoothstep(0.35, 0.5, abs(sin(ang * 6.0))) * (1.0 - smoothstep(0.30, 0.34, r)) ;
    float rings = smoothstep(0.4, 0.5, abs(sin(r * 34.0))) * smoothstep(0.34, 0.38, r) * (1.0 - smoothstep(0.62, 0.66, r));
    albedo = vec3(0.07) + vec3(0.05) * (rim + 0.7 * rings) + vec3(0.025) * spokes;
    a = disc;
    rough = 0.5;
    float ring = (1.0 - smoothstep(0.86, 0.9, r)) * smoothstep(0.82, 0.86, r);
    albedo = mix(albedo, vec3(0.012), ring);
    a = max(a, ring);
  } else if (k < 5.5) {                              // 5: kerb drain grate
    vec2 gp = p * vec2(1.0, 1.0);
    float body = 1.0 - smoothstep(0.92, 0.98, max(abs(gp.x), abs(gp.y)));
    float bars = smoothstep(0.35, 0.5, abs(sin(gp.y * 3.14159 * 5.0 + 0.5)));
    float frame = smoothstep(0.80, 0.84, max(abs(gp.x), abs(gp.y)));
    albedo = mix(vec3(0.005), vec3(0.09), max(bars, frame));
    a = body;
    rough = 0.55;
  } else if (k < 6.5) {                              // 6: tyre marks (two lanes, fading at both ends)
    float tl = 1.0 - smoothstep(0.0, 0.14, abs(abs(p.x) - 0.55) - 0.0);
    float along = smoothstep(1.0, 0.55, abs(p.y)) * (0.55 + 0.45 * fbm(vec2(p.y * 7.0, seed.x * 30.0 + p.x * 3.0)));
    a = tl * along * 0.55;
    albedo = vec3(0.012);
    rough = 0.9;
  } else if (k < 7.5) {                              // 7: graffiti tag (fictional abstract lettering)
    vec2 q = p * 1.15;
    float d = 1e9;
    // a handful of looping marker strokes seeded per decal
    for (int i = 0; i < 5; ++i) {
      float fi = float(i);
      vec2 a0 = vec2(-0.8 + fi * 0.40, (h21(vec2(fi, seed.x)) - 0.5) * 0.9);
      vec2 b0 = a0 + vec2(0.34, (h21(vec2(fi, seed.y)) - 0.5) * 1.3);
      vec2 mid = (a0 + b0) * 0.5 + vec2(0.0, (h21(vec2(seed.x, fi + 3.0)) - 0.5) * 0.7);
      d = min(d, min(segDist(q, a0, mid), segDist(q, mid, b0)));
    }
    float stroke = 1.0 - smoothstep(0.045, 0.075, d);
    float outline = (1.0 - smoothstep(0.095, 0.13, d)) - stroke;
    vec3 col1 = 0.5 + 0.5 * cos(6.2831 * (seed.x + vec3(0.0, 0.33, 0.67)));
    vec3 col2 = vec3(0.04);
    albedo = mix(col2, col1, stroke);
    a = max(stroke, outline * 0.9) * 0.92;
    rough = 0.45;
  } else if (k < 8.5) {                              // 8: leaf litter
    float acc = 0.0;
    vec3 tint = vec3(0.0);
    for (int i = 0; i < 14; ++i) {
      float fi = float(i);
      vec2 c = vec2(h21(vec2(fi, seed.x)), h21(vec2(fi + 7.0, seed.y))) * 1.7 - 0.85;
      float ang = h21(vec2(fi, 3.0)) * 6.28;
      vec2 d = p - c;
      d = vec2(cos(ang) * d.x + sin(ang) * d.y, -sin(ang) * d.x + cos(ang) * d.y);
      float m = 1.0 - smoothstep(0.7, 1.0, length(d * vec2(1.0, 2.6)) / 0.12);
      float hue = h21(vec2(fi, 9.0));
      vec3 lc = mix(vec3(0.20, 0.30, 0.07), vec3(0.42, 0.26, 0.07), hue);
      tint += lc * m;
      acc = max(acc, m);
    }
    a = acc * 0.95;
    albedo = tint / max(acc, 1e-3);
  } else if (k < 9.5) {                              // 9: asphalt patch (newer, darker, hard edge)
    float d = sdRoundBox(p, vec2(0.9, 0.9), 0.05);
    float rim = 1.0 - smoothstep(0.0, 0.05, abs(d + 0.01));
    a = (1.0 - smoothstep(-0.02, 0.0, d)) * 0.95;
    albedo = vec3(0.045) * (0.8 + 0.5 * fbm(p * 14.0 + seed * 9.0));
    albedo = mix(albedo, vec3(0.02), rim);
    rough = 0.7;
  } else if (k > 10.5 && k < 11.5) {                 // 11: grime streaks running down a wall
    float cols = floor((p.x * 0.5 + 0.5) * 9.0);
    float cx = (cols + 0.5) / 9.0 * 2.0 - 1.0;
    float len = 0.35 + 0.6 * h21(vec2(cols, seed.x * 7.0));
    float w = 0.05 + 0.05 * h21(vec2(cols, seed.y * 5.0));
    float streak = (1.0 - smoothstep(0.0, w, abs(p.x - cx))) * smoothstep(1.0, 1.0 - len, p.y * -1.0 + 0.0) * smoothstep(-1.0, -0.7, -p.y) ;
    streak = (1.0 - smoothstep(0.0, w, abs(p.x - cx + (fbm(vec2(p.y * 3.0, cols)) - 0.5) * 0.08))) * smoothstep(-1.0, 1.0 - 2.0 * len, p.y);
    streak *= step(0.35, h21(vec2(cols, 4.0 + seed.x)));
    a = streak * (0.35 + 0.35 * fbm(p * 6.0)) * edge;
    albedo = vec3(0.03, 0.028, 0.025);
    rough = 0.95;
  } else if (k > 11.5 && k < 12.5) {                 // 12: paper poster (fictional bands / events)
    float d = sdRoundBox(p, vec2(0.92), 0.02);
    float paper = 1.0 - smoothstep(-0.01, 0.0, d);
    vec3 bg = 0.55 + 0.45 * cos(6.2831 * (seed.x + vec3(0.0, 0.33, 0.67)));
    float band = smoothstep(0.1, 0.12, abs(p.y - 0.45)) ;
    float blocks = step(0.5, fbm(vec2(floor(p.x * 5.0), floor(p.y * 9.0)) * 1.7 + seed * 30.0));
    float text = (1.0 - band) * 0.0 + step(p.y, 0.0) * blocks * 0.8;
    albedo = mix(bg, vec3(0.06), text);
    albedo = mix(albedo, vec3(0.95, 0.93, 0.85), step(0.55, p.y) * step(0.5, fbm(vec2(floor(p.x * 7.0), floor(p.y * 7.0)) + seed * 9.0)) * 0.9);
    albedo *= 0.8 + 0.2 * fbm(p * 9.0);
    a = paper * 0.97;
    rough = 0.7;
  } else {                                           // 10: wet patch / puddle stain
    float r = length(p) + (fbm(p * 2.5 + seed * 17.0) - 0.5) * 0.6;
    a = (1.0 - smoothstep(0.45, 0.95, r)) * 0.5;
    albedo = vec3(0.02);
    rough = 0.12;
    sheen = 0.9 * (1.0 - smoothstep(0.1, 0.8, r));
  }
  a *= edge * vAK.x;
  if (a < 0.003) discard;
  // lighting of an up-facing surface
  vec3 V = normalize(g.camPos.xyz - vWorld);
  vec3 N = normalize(vNormal);
  float sh = shadowTerm(vWorld) * (1.0 - g.params.w);
  float NoL = max(dot(N, g.sunDir.xyz), 0.0);
  float skyVis;
  vec3 amb = probeAmbient(vWorld + N * 0.3, N, skyVis);
  vec3 lit = albedo * (g.sunColor.rgb * NoL * sh / PI + amb) + evalLights(vWorld, N, V, albedo, vec3(0.04), rough);
  // sky glints on slick surfaces (oil, wet) and a touch on metal covers
  vec3 R = reflect(-V, N);
  float NoV = max(dot(N, V), 1e-3);
  vec3 env = skyRadiance(R, 0.0) * envBRDF(vec3(0.04), rough, NoV);
  lit += env * (sheen + 0.25 * (1.0 - rough)) * (1.0 - 0.0);
  lit = applyFog(lit, vWorld);
  outColor = vec4(lit, a);
}
