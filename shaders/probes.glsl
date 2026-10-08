// Baked ambient visibility probes (see World::probes). Needs globals.glsl first.
layout(set = 0, binding = 2) uniform sampler2DArray uProbe;

// Local ambient irradiance for normal N at P and the sky visibility (0..1) in that hemisphere (also used for specular occlusion).
vec3 probeAmbient(vec3 P, vec3 N, out float skyVis) {
  if (g.probeInfo.x < 0.5) {
    skyVis = 1.0;
    return mix(g.ambGround.rgb, g.ambSky.rgb, N.y * 0.5 + 0.5);
  }
  vec2 uv = (P.xz - g.probeRect.xy) * g.probeRect.zw;
  // the grid stores two heights: eye level and rooftop level
  float hy = clamp((P.y - g.probeInfo.y) / max(g.probeInfo.z - g.probeInfo.y, 1.0), 0.0, 1.0);
  vec4 a = mix(texture(uProbe, vec3(uv, 0.0)), texture(uProbe, vec3(uv, 2.0)), hy);
  vec4 b = mix(texture(uProbe, vec3(uv, 1.0)), texture(uProbe, vec3(uv, 3.0)), hy);
  vec3 w = N * N;
  float vx = N.x > 0.0 ? a.x : a.y;
  float vy = N.y > 0.0 ? a.z : a.w;
  float vz = N.z > 0.0 ? b.x : b.y;
  vec3 side = mix(g.ambGround.rgb, g.ambSky.rgb, 0.5);
  vec3 colY = N.y > 0.0 ? g.ambSky.rgb : g.ambGround.rgb * mix(vec3(1.0), clamp(vec3(b.z, b.w, max(0.0, 1.0 - b.z - b.w)) * 3.0, 0.4, 1.8), 0.75);
  // bounce light where the sky is hidden: walls and ground reflect part of what hits them
  // b.zw: chromaticity of the surrounding surfaces (baked); turns the grey bounce into coloured one-bounce light
  vec3 tint = vec3(b.z, b.w, max(0.0, 1.0 - b.z - b.w)) * 3.0;
  tint = mix(vec3(1.0), clamp(tint, 0.4, 1.8), 0.75);
  vec3 bounce = (g.ambGround.rgb * 0.85 + g.sunColor.rgb * 0.035 * max(g.sunDir.y, 0.0)) * tint;
  vec3 amb = w.x * (vx * side + (1.0 - vx) * bounce) + w.y * (vy * colY + (1.0 - vy) * bounce) + w.z * (vz * side + (1.0 - vz) * bounce);
  skyVis = dot(w, vec3(vx, vy, vz));
  return amb;
}
