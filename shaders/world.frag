#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(set = 0, binding = 1) uniform sampler2DShadow uShadow;
layout(set = 1, binding = 0) uniform sampler2DArray uMat;
layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vUVL;
layout(location = 3) in vec4 vColor;
layout(location = 4) in vec4 vShadow;
layout(location = 0) out vec4 outColor;

float shadowTerm(vec4 sc) {
  vec3 p = sc.xyz / sc.w;
  vec2 uv = p.xy * 0.5 + 0.5;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0) return 1.0;
  float t = g.params.y;
  float s = texture(uShadow, vec3(uv, p.z)) * 0.28;
  s += texture(uShadow, vec3(uv + vec2(t, 0.0), p.z)) * 0.18;
  s += texture(uShadow, vec3(uv - vec2(t, 0.0), p.z)) * 0.18;
  s += texture(uShadow, vec3(uv + vec2(0.0, t), p.z)) * 0.18;
  s += texture(uShadow, vec3(uv - vec2(0.0, t), p.z)) * 0.18;
  // fade the shadow out toward the edge of the shadow volume
  vec2 e = abs(uv - 0.5) * 2.0;
  float edge = smoothstep(0.85, 1.0, max(e.x, e.y));
  return mix(s, 1.0, edge);
}

void main() {
  vec3 N = normalize(vNormal);
  vec4 tex = texture(uMat, vUVL);
  vec3 albedo = tex.rgb * vColor.rgb;
  float ndl = max(dot(N, g.sunDir.xyz), 0.0);
  float sh = shadowTerm(vShadow);
  float hemi = N.y * 0.5 + 0.5;
  vec3 amb = mix(g.ambGround.rgb, g.ambSky.rgb, hemi) * vColor.a;
  vec3 lit = albedo * (amb + g.sunColor.rgb * ndl * mix(1.0, sh, g.sunDir.w));
  float dist = length(vWorld - g.camPos.xyz);
  float f = 1.0 - exp(-dist * g.fog.w);
  lit = mix(lit, g.fog.rgb, clamp(f, 0.0, 0.9));
  outColor = vec4(encodeDisplay(lit), 1.0);
}
