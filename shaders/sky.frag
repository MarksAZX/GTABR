#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() {
  vec2 ndc = vUV * 2.0 - 1.0;
  vec4 a = g.invViewProj * vec4(ndc, 0.0, 1.0);
  vec4 b = g.invViewProj * vec4(ndc, 1.0, 1.0);
  vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
  float h = clamp(dir.y, -0.2, 1.0);
  vec3 horizon = g.fog.rgb * 1.15;
  vec3 zenith = g.ambSky.rgb * vec3(0.55, 0.80, 1.55) + vec3(0.02, 0.07, 0.16);
  vec3 col = mix(horizon, zenith, pow(clamp(h, 0.0, 1.0), 0.55));
  float sd = max(dot(dir, g.sunDir.xyz), 0.0);
  col += g.sunColor.rgb * (pow(sd, 400.0) * 3.0 + pow(sd, 12.0) * 0.18);
  // soft procedural clouds
  vec2 cp = dir.xz / max(dir.y + 0.35, 0.12) * 1.3 + vec2(g.camPos.w * 0.004, 0.0);
  float n = sin(cp.x * 2.1) * sin(cp.y * 2.7 + 1.3) + 0.5 * sin(cp.x * 5.3 + 2.0) * sin(cp.y * 4.1);
  col = mix(col, vec3(1.0, 0.93, 0.85) * 1.1, smoothstep(0.35, 1.1, n) * 0.35 * smoothstep(0.0, 0.25, dir.y));
  outColor = vec4(encodeDisplay(col), 1.0);
}
