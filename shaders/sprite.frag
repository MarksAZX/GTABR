#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(set = 1, binding = 0) uniform sampler2D uAtlas;
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vTint;
layout(location = 2) in float vDist;
layout(location = 0) out vec4 outColor;
void main() {
  vec4 t = texture(uAtlas, vUV);
  float a = t.a * vTint.a;
  if (a < 0.02) discard;
  // Sprites are baked with neutral studio lighting; match the scene's overall light level.
  vec3 light = g.ambSky.rgb * 0.55 + g.sunColor.rgb * 0.42 + vec3(0.18);
  vec3 lit = t.rgb * vTint.rgb * light;
  float f = 1.0 - exp(-vDist * g.fog.w);
  lit = mix(lit, g.fog.rgb, clamp(f, 0.0, 0.9));
  outColor = vec4(encodeDisplay(lit), a);
}
