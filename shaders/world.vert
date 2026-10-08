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
  if (aLayer > 30.5 && aLayer < 31.5) {
    float phase = aUV.y * 0.55 - g.camPos.w * 1.1;
    float amp = smoothstep(0.0, 5.0, aUV.y) * (1.0 - smoothstep(20.0, 45.0, aUV.y)) * 0.09;
    vWorld.y += sin(phase) * amp + sin(aUV.x * 2.0 + g.camPos.w) * amp * 0.4;
    vNormal = vec3(0, 1, 0); // fine water normals are evaluated in world space in the fragment shader
  }
  gl_Position = g.viewProj * vec4(vWorld, 1.0);
}
