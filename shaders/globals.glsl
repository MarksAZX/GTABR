// Shared per-frame uniform block (set 0) + lighting helpers. Scene shaders output LINEAR HDR radiance;
// exposure, bloom, tone mapping and grading happen in composite.frag.
struct Light {
  vec4 posRadius;   // xyz position, w radius
  vec4 colorInt;    // rgb colour * intensity
  vec4 dirCone;     // xyz spot direction, w = cos(cone) (spot) or -2 (point)
};
layout(set = 0, binding = 0, std140) uniform Globals {
  mat4 viewProj;
  mat4 view;
  mat4 invViewProj;
  mat4 lightViewProj[2];
  vec4 camPos;      // xyz, w = time (s)
  vec4 camRight;
  vec4 camUp;
  vec4 camFwd;
  vec4 sunDir;      // xyz towards the sun (or moon at night), w = shadow strength
  vec4 sunColor;    // rgb irradiance, w = sun disk intensity
  vec4 ambSky;      // rgb sky irradiance
  vec4 ambGround;   // rgb ground bounce
  vec4 fog;         // rgb inscatter colour, w = density
  vec4 params;      // x = exposure (unused here), y = shadow texel, z = night factor, w = indoor factor
  vec4 sky0;        // rgb zenith, w = cloud coverage
  vec4 sky1;        // rgb horizon, w = cloud brightness
  vec4 cascade;     // x = split distance, y = cascade count, z = fog height falloff, w = wetness
  vec4 lightInfo;   // x = light count
  vec4 probeRect;   // x0, z0, 1/width, 1/depth of the ambient probe grid
  vec4 probeInfo;   // x = enabled, y = ground layer height, z = rooftop layer height
  vec4 lightGrid;   // x = light count, y,z = tiles per pixel, w = tiles in x
} g;
#ifdef GL_FRAGMENT_SHADER
// Dynamic lights (up to 128) and per-tile light lists, written by the renderer every frame. std430: 128 x 3 vec4, then tiles of 33 uints.
layout(set = 0, binding = 3, std430) readonly buffer LightData {
  vec4 lightVec[384];
  uint tileData[];
} lb;
#endif

const float PI = 3.14159265;

float D_GGX(float NoH, float a) {
  float a2 = a * a;
  float d = NoH * NoH * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d + 1e-6);
}
float V_SmithJointApprox(float NoV, float NoL, float a) {
  float gv = NoL * (NoV * (1.0 - a) + a);
  float gl = NoV * (NoL * (1.0 - a) + a);
  return 0.5 / max(gv + gl, 1e-5);
}
vec3 F_Schlick(vec3 f0, float VoH) { return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0); }
// Karis' analytic approximation of the split-sum environment BRDF (mobile friendly)
vec3 envBRDF(vec3 f0, float rough, float NoV) {
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 r = rough * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
  vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
  return f0 * ab.x + ab.y;
}

// Analytic sky radiance used for the background and as a reflection / ambient source.
vec3 skyRadiance(vec3 dir, float sunDisk) {
  float h = dir.y;
  vec3 zen = g.sky0.rgb, hor = g.sky1.rgb;
  vec3 col = mix(hor, zen, pow(clamp(h, 0.0, 1.0), 0.42));
  // warm band near the horizon toward the sun
  float sd = max(dot(dir, g.sunDir.xyz), 0.0);
  float towardSun = pow(max(dot(normalize(vec3(dir.x, 0.0, dir.z) + 1e-4), normalize(vec3(g.sunDir.x, 0.0, g.sunDir.z) + 1e-4)), 0.0), 3.0);
  col += g.sunColor.rgb * 0.05 * towardSun * exp(-max(h, 0.0) * 6.0);
  col += g.sunColor.rgb * (0.025 * pow(sd, 6.0) + 0.18 * pow(sd, 120.0));
  col += g.sunColor.rgb * sunDisk * g.sunColor.w * smoothstep(0.99955, 0.9998, sd);
  // below the horizon: dark ground haze
  vec3 ground = g.ambGround.rgb * 0.55 + hor * 0.25;
  col = mix(col, ground, smoothstep(0.0, -0.18, h));
  return col;
}

vec3 applyFog(vec3 col, vec3 worldPos) {
  vec3 d = worldPos - g.camPos.xyz;
  float dist = length(d);
  // exponential distance fog thinned with height
  float hf = exp(-max(worldPos.y, 0.0) * g.cascade.z);
  float f = 1.0 - exp(-dist * g.fog.w * hf);
  vec3 fogCol = g.fog.rgb;
  vec3 dir = d / max(dist, 1e-3);
  fogCol += g.sunColor.rgb * 0.08 * pow(max(dot(dir, g.sunDir.xyz), 0.0), 8.0);
  return mix(col, fogCol, clamp(f, 0.0, 0.92));
}

#ifdef GL_FRAGMENT_SHADER
// Point / spot light contribution (Lambert + GGX) with smooth windowed falloff. Only the lights that touch this pixel's screen tile are visited.
vec3 evalLights(vec3 P, vec3 N, vec3 V, vec3 albedo, vec3 f0, float rough) {
  vec3 acc = vec3(0.0);
  int tilesX = int(g.lightGrid.w);
  ivec2 tile = ivec2(gl_FragCoord.xy * g.lightGrid.yz);
  tile = clamp(tile, ivec2(0), ivec2(tilesX - 1, 8));
  uint base = uint(tile.y * tilesX + tile.x) * 33u;
  uint cnt = min(lb.tileData[base], 32u);
  for (uint k = 0u; k < cnt; ++k) {
    uint idx = lb.tileData[base + 1u + k];
    vec4 pr = lb.lightVec[idx * 3u], ci = lb.lightVec[idx * 3u + 1u], dc = lb.lightVec[idx * 3u + 2u];
    vec3 L = pr.xyz - P;
    float d2 = dot(L, L);
    float r = pr.w;
    if (d2 > r * r) continue;
    float d = sqrt(d2);
    L /= d;
    float win = clamp(1.0 - pow(d / r, 4.0), 0.0, 1.0);
    float att = win * win / (d2 + 1.0);
    if (dc.w > -1.5) {
      float c = dot(-L, dc.xyz);
      att *= smoothstep(dc.w, dc.w + 0.12, c);
    }
    float NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0 || att <= 0.0) continue;
    vec3 H = normalize(L + V);
    float NoV = max(dot(N, V), 1e-3), NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
    float a = rough * rough;
    vec3 spec = D_GGX(NoH, a) * V_SmithJointApprox(NoV, NoL, a) * F_Schlick(f0, VoH);
    acc += (albedo / PI + spec) * ci.rgb * NoL * att;
  }
  return acc;
}
#endif
