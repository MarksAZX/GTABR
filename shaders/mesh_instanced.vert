#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(push_constant) uniform PC { mat4 model; vec4 tint; vec4 params; } pc;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;
layout(location = 6) in vec4 iModel0;
layout(location = 7) in vec4 iModel1;
layout(location = 8) in vec4 iModel2;
layout(location = 9) in vec4 iModel3;
layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vTangent;
layout(location = 3) out vec2 vUV;
void main() {
  mat4 model = mat4(iModel0,iModel1,iModel2,iModel3);
  vec4 wp = model * vec4(aPos, 1.0);
  vWorld = wp.xyz;
  mat3 m = mat3(model);
  vNormal = m * aNormal.xyz;
  vTangent = vec4(m * aTangent.xyz, aTangent.w);
  vUV = aUV;
  gl_Position = g.viewProj * wp;
}
