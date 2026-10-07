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
  // baked studio lighting -> scale by the scene's light level
  vec3 outdoorLight = g.ambSky.rgb * 0.55 + g.sunColor.rgb * 0.11;
  vec3 light = mix(outdoorLight, vec3(1.0, 0.95, 0.86) * 1.1, g.params.w);
  vec3 lit = t.rgb * vTint.rgb * light;
  float f = 1.0 - exp(-vDist * g.fog.w);
  lit = mix(lit, g.fog.rgb, clamp(f, 0.0, 0.9));
  outColor = vec4(lit, a);
}
