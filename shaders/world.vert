#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aNormal;  // snorm, w = emissive strength
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;   // unorm: rgb tint, a = baked AO
layout(location = 4) in float aLayer;
layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vUVL;
layout(location = 3) out vec4 vColor;
layout(location = 4) out float vEmissive;
void main() {
  vWorld = aPos;
  vNormal = aNormal.xyz;
  vUVL = vec3(aUV, aLayer);
  vColor = aColor;
  vEmissive = max(aNormal.w, 0.0);
  gl_Position = g.viewProj * vec4(aPos, 1.0);
}
