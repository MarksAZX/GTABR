#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(push_constant) uniform PC { mat4 model; vec4 tint; vec4 params; } pc;
layout(set = 2, binding = 0, std140) uniform Bones { mat4 bones[64]; } sk;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUV;
layout(location = 4) in uvec4 aJoints;
layout(location = 5) in vec4 aWeights;
layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vTangent;
layout(location = 3) out vec2 vUV;
void main() {
  mat4 skin = aWeights.x * sk.bones[aJoints.x] + aWeights.y * sk.bones[aJoints.y] + aWeights.z * sk.bones[aJoints.z] + aWeights.w * sk.bones[aJoints.w];
  vec4 wp = pc.model * (skin * vec4(aPos, 1.0));
  vWorld = wp.xyz;
  mat3 m = mat3(pc.model) * mat3(skin);
  vNormal = m * aNormal.xyz;
  vTangent = vec4(m * aTangent.xyz, aTangent.w);
  vUV = aUV;
  gl_Position = g.viewProj * wp;
}
