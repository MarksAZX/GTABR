#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(set = 0, binding = 1) uniform sampler2DArrayShadow uShadow;
layout(set = 1, binding = 0) uniform sampler2DArray uMat;    // albedo
layout(set = 1, binding = 1) uniform sampler2DArray uMatN;   // rg = normal xy, b = roughness, a = cavity / AO
#include "shadowing.glsl"
#include "probes.glsl"
#include "material_ids.glsl"
#include "water.glsl"
layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vUVL;
layout(location = 3) in vec4 vColor;
layout(location = 4) in float vEmissive;
layout(location = 0) out vec4 outColor;

vec3 perturb(vec3 N, vec3 P, vec2 uv, vec3 tn) {
  // cotangent frame from screen-space derivatives (no stored tangents on the world mesh)
  vec3 dp1 = dFdx(P), dp2 = dFdy(P);
  vec2 du1 = dFdx(uv), du2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
  vec3 T = dp2perp * du1.x + dp1perp * du2.x;
  vec3 B = dp2perp * du1.y + dp1perp * du2.y;
  float invmax = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);
  mat3 TBN = mat3(T * invmax, B * invmax, N);
  return normalize(TBN * tn);
}

float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}

vec3 shadeWater() {
  float t = g.camPos.w;
  float shore = vUVL.y;
  vec2 dd;
  waves(vWorld.xz, t, waveAmp(shore), dd);
  // fine ripples from the texture layer (scrolling in two directions) on top of the analytic swell
  vec2 r1 = texture(uMatN, vec3(vWorld.xz * 0.11 + vec2(t * 0.02, t * 0.013), vUVL.z)).rg * 2.0 - 1.0;
  vec2 r2 = texture(uMatN, vec3(vWorld.xz * 0.23 - vec2(t * 0.017, -t * 0.021), vUVL.z)).rg * 2.0 - 1.0;
  vec3 N = normalize(vec3(-dd.x, 1.0, -dd.y) * 1.7 + vec3(r1.x + r2.x, 0.0, r1.y + r2.y) * 0.16);
  vec3 V = normalize(g.camPos.xyz - vWorld);
  float NoV = max(dot(N, V), 1e-3);
  float fres = 0.02 + 0.98 * pow(1.0 - NoV, 5.0);
  vec3 R = reflect(-V, N);
  R.y = abs(R.y);
  vec3 refl = skyRadiance(R, 1.0);
  float indoor = g.params.w;
  // body colour: baked depth tint (turquoise shallows -> deep blue), lit by sky + sun, with a little subsurface glow
  vec3 depthTint = mix(vec3(0.16, 0.62, 0.58), vec3(0.015, 0.11, 0.22), smoothstep(0.0, 38.0, shore));
  vec3 body = depthTint * (g.ambSky.rgb * 0.55 + g.sunColor.rgb * max(g.sunDir.y, 0.0) * 0.16);
  float sss = pow(max(dot(V, -g.sunDir.xyz) * 0.5 + 0.5, 0.0), 4.0) * clamp(dd.x + dd.y + 0.3, 0.0, 1.0);
  body += vec3(0.1, 0.45, 0.4) * g.sunColor.rgb * sss * 0.12;
  // sun glitter
  vec3 H = normalize(g.sunDir.xyz + V);
  float spec = pow(max(dot(N, H), 0.0), 900.0) * 60.0 + pow(max(dot(N, H), 0.0), 120.0) * 1.2;
  float sh = shadowTerm(vWorld) * (1.0 - indoor);
  vec3 col = mix(body, refl, fres) + g.sunColor.rgb * spec * sh;
  // foam: breaking lines that run up the beach, plus white caps on the steepest crests offshore
  float n = vnoise(vWorld.xz * 0.9 + t * 0.3) * 0.6 + vnoise(vWorld.xz * 2.7 - t * 0.2) * 0.4;
  float surf = smoothstep(7.0, 0.0, shore);
  float bands = smoothstep(0.72, 0.97, sin(shore * 1.3 + t * 1.6 + n * 2.0) * 0.5 + 0.5) * surf;
  float edge = smoothstep(1.6, 0.0, shore + n * 0.8 + sin(t * 0.9 + vWorld.x * 0.05 + vWorld.z * 0.05) * 0.6);
  float nf = vnoise(vWorld.xz * 3.1 + t * 0.5);
  float caps = smoothstep(0.55, 0.85, length(dd)) * smoothstep(0.7, 0.95, nf) * (1.0 - surf);
  float foam = clamp(max(max(bands * (0.55 + 0.45 * n), edge), caps * 0.7), 0.0, 1.0);
  vec3 foamCol = (g.ambSky.rgb * 1.1 + g.sunColor.rgb * max(g.sunDir.y, 0.0) * 0.55) * (0.6 + 0.4 * sh);
  col = mix(col, foamCol, foam * 0.75);
  col += evalLights(vWorld, N, V, vec3(0.02), vec3(0.02), 0.08);
  return applyFog(col, vWorld);
}

void main() {
  if (int(vUVL.z + 0.5) == MAT_WATER) { outColor = vec4(shadeWater(), 1.0); return; }
  vec3 Ng = normalize(vNormal);
  vec4 tex = texture(uMat, vUVL);
  vec4 nr = texture(uMatN, vUVL);
  if (int(vUVL.z + 0.5) == MAT_FOLIAGE) {
    // leaf cut-out: the dark gaps between the leaves of the canopy texture become holes, more of them toward the silhouette,
    // which gives ragged leafy edges and lets light and the trunk show through instead of a solid blob
    float lum = dot(tex.rgb, vec3(0.30, 0.59, 0.11));
    vec3 Vv = normalize(g.camPos.xyz - vWorld);
    float rim = pow(1.0 - abs(dot(normalize(vNormal), Vv)), 2.2);
    if (lum < 0.012 + 0.04 * rim) discard;
  }
  vec3 albedo = tex.rgb * vColor.rgb;
  // macro variation over the ground so the tiling never reads as a repeating pattern (patchy lawn, worn asphalt, sun-bleached sand)
  {
    int gl = int(vUVL.z + 0.5);
    if (gl <= MAT_PEDRA_PORT || gl == MAT_GRASS || gl == MAT_SAND || gl == MAT_DIRT) {
      float macro = vnoise(vWorld.xz * 0.045) * 0.55 + vnoise(vWorld.xz * 0.16 + 3.0) * 0.30 + vnoise(vWorld.xz * 0.7 + 9.0) * 0.15;
      if (gl == MAT_GRASS) albedo *= mix(vec3(0.74, 0.82, 0.66), vec3(1.18, 1.10, 0.84), macro);
      else albedo *= mix(0.82, 1.14, macro);
    }
  }
  vec3 tn = vec3(nr.rg * 2.0 - 1.0, 0.0);
  tn.z = sqrt(max(1.0 - dot(tn.xy, tn.xy), 0.0));
  vec3 N = perturb(Ng, vWorld, vUVL.xy, tn);
  float rough = clamp(nr.b, 0.04, 1.0);
  int layerId = int(vUVL.z + 0.5);
  float ao = vColor.a * mix(1.0, nr.a, 0.85);
  // rain-darkened / wet ground: darker albedo, glossier on horizontal surfaces
  float wet = g.cascade.w * smoothstep(0.7, 0.95, Ng.y);
  albedo *= 1.0 - 0.35 * wet;
  rough = mix(rough, 0.12, wet * 0.85);
  // puddles on paved ground: mirror-like patches that reflect the sky, with raindrop ripples while it rains
  if (layerId <= 3 && wet > 0.05) {
    float pn = vnoise(vWorld.xz * 0.27) * 0.6 + vnoise(vWorld.xz * 0.85 + 7.0) * 0.4;
    float pud = clamp(g.cascade.w, 0.0, 1.0) * smoothstep(0.50, 0.60, pn) * smoothstep(0.85, 0.97, Ng.y);
    float rr = g.lightInfo.z;
    if (rr > 0.03 && pud > 0.01) {
      vec2 cell = floor(vWorld.xz * 2.2);
      vec2 f = fract(vWorld.xz * 2.2) - 0.5;
      float ph = fract(g.camPos.w * 1.3 + hash12(cell) * 6.0);
      float ring = abs(length(f) - ph * 0.5);
      N.xz += normalize(f + 1e-4) * smoothstep(0.05, 0.0, ring) * (1.0 - ph) * 0.35 * rr * pud;
      N = normalize(N);
    }
    N = normalize(mix(N, Ng, pud * 0.9));
    rough = mix(rough, 0.03, pud);
    albedo *= 1.0 - 0.35 * pud;
  }

  vec3 V = normalize(g.camPos.xyz - vWorld);
  vec3 L = g.sunDir.xyz;
  vec3 H = normalize(L + V);
  float NoL = max(dot(N, L), 0.0), NoV = max(dot(N, V), 1e-3), NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
  // bare metal layers (shutters, metal roofs, rails): coloured specular, no diffuse - values mirror METAL_LAYERS in build_assets.py
  float metal = (layerId == MAT_ROOF_METAL) ? 0.75 : ((layerId == MAT_METAL) ? 0.25 : 0.0);   // painted street furniture only a little
  vec3 f0 = mix(vec3(0.04), albedo, metal);
  albedo *= 1.0 - metal;
  float a = rough * rough;
  float indoor = g.params.w;
  float sh = shadowTerm(vWorld) * (1.0 - indoor);
  vec3 direct = (albedo / PI + D_GGX(NoH, a) * V_SmithJointApprox(NoV, NoL, a) * F_Schlick(f0, VoH)) * g.sunColor.rgb * NoL * sh;

  // ambient: hemisphere irradiance + analytic sky reflection
  float hemi = N.y * 0.5 + 0.5;
  float skyVis;
  vec3 irr = probeAmbient(vWorld, N, skyVis);
  vec3 R = reflect(-V, N);
  float specVis;
  probeAmbient(vWorld, R, specVis);
  vec3 env = skyRadiance(R, 0.0) * (1.0 - 0.6 * a) * mix(0.15, 1.0, specVis * specVis);
  vec3 lamp = vec3(1.0, 0.94, 0.84) * (0.75 + 0.25 * N.y);
  irr = mix(irr, lamp, indoor);
  env = mix(env, lamp * 0.6, indoor);
  vec3 ambient = (albedo * irr + env * envBRDF(f0, rough, NoV)) * ao;

  vec3 col = direct + ambient + evalLights(vWorld, N, V, albedo, f0, rough);
  col += albedo * vEmissive * g.params.z * 6.0;   // shop signs / lamps glow at night
  // lit windows at night: the dark glass areas of the facade textures glow warm, a different random subset per window cell
  bool isFacade = (layerId >= MAT_HOUSE_YELLOW && layerId <= MAT_APT_BANDS) || layerId == MAT_HOUSE_PERIFERIA_A || layerId == MAT_HOUSE_PERIFERIA_B || layerId == MAT_APT_TOWER;
  if (g.params.z > 0.05 && g.params.w < 0.5 && isFacade) {
    float lumT = dot(tex.rgb, vec3(0.30, 0.59, 0.11));
    float glass = smoothstep(0.075, 0.02, lumT) * step(tex.r * 0.9, tex.b + 0.01) * step(abs(Ng.y), 0.5);
    vec2 cell = floor(vUVL.xy * vec2(5.0, 6.0));
    float on = step(0.46, hash12(cell + float(layerId) * 3.7));
    float flick = 0.75 + 0.25 * hash12(cell + 11.0);
    vec3 warm = mix(vec3(1.0, 0.78, 0.48), vec3(0.75, 0.88, 1.0), step(0.82, hash12(cell + 5.0)));   // a few TV-blue rooms
    col += warm * glass * on * flick * g.params.z * 2.4;
  }
  col = applyFog(col, vWorld);
  outColor = vec4(col, 1.0);
}
