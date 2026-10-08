#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
#include "material_ids.glsl"
#include "water.glsl"
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
  vec3 P = aPos;
  if (int(aLayer + 0.5) == MAT_WATER) {
    vec2 dd;
    P.y += waves(P.xz, g.camPos.w, waveAmp(aUV.y), dd);
  }
  if (int(aLayer + 0.5) == MAT_FOLIAGE) {
    // wind: height-weighted sway of leaves and fronds, stronger gusts with the weather
    float w = g.lightInfo.y;
    float h = clamp((P.y - 1.2) / 6.0, 0.0, 1.0);
    float t = g.camPos.w;
    float ph = P.x * 0.31 + P.z * 0.23;
    vec2 dirw = vec2(0.82, 0.57);
    float s1 = sin(t * 1.35 + ph) * 0.6 + sin(t * 3.1 + ph * 2.3) * 0.25;
    P.xz += dirw * s1 * w * h * 0.22 + vec2(-dirw.y, dirw.x) * sin(t * 2.2 + ph * 1.7) * w * h * 0.07;
    P.y += abs(s1) * w * h * -0.05;
  }
  vWorld = P;
  vNormal = aNormal.xyz;
  vUVL = vec3(aUV, aLayer);
  vColor = aColor;
  vEmissive = max(aNormal.w, 0.0);
  gl_Position = g.viewProj * vec4(P, 1.0);
}
