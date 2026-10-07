#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(set = 1, binding = 0) uniform sampler2D uAtlas;
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vTint;
layout(location = 2) in float vDist;
layout(location = 3) in float vEmissive;
layout(location = 0) out vec4 outColor;
// Drawn only where world geometry hides the sprite: a soft tinted outline so the player never gets lost.
void main() {
  vec4 t = texture(uAtlas, vUV);
  float a = t.a * 0.38;
  if (a < 0.02) discard;
  outColor = vec4(mix(t.rgb * vec3(0.8, 0.9, 1.2), vec3(0.45, 0.8, 1.0), 0.6), a);
}
