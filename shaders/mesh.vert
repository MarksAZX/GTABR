#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(push_constant) uniform PC { mat4 model; vec4 tint; vec4 params; } pc;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;
layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vTangent;
layout(location = 3) out vec2 vUV;
void main() {
  vec4 wp = pc.model * vec4(aPos, 1.0);
  wp.xyz+=foliageWind(wp.xyz,max(aNormal.w,0.0),aPos.y);
  vWorld = wp.xyz;
  mat3 m = mat3(pc.model);
  vNormal = m * aNormal.xyz;
  vTangent = vec4(m * aTangent.xyz, aTangent.w);
  vUV = aUV;
  gl_Position = g.viewProj * wp;
}
