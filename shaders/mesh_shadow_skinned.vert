#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(push_constant) uniform PC { mat4 model; int cascade; } pc;
layout(set = 1, binding = 0, std140) uniform Bones { mat4 bones[64]; } sk;
layout(location = 0) in vec3 aPos;
layout(location = 4) in uvec4 aJoints;
layout(location = 5) in vec4 aWeights;
void main() {
  mat4 skin = aWeights.x * sk.bones[aJoints.x] + aWeights.y * sk.bones[aJoints.y] + aWeights.z * sk.bones[aJoints.z] + aWeights.w * sk.bones[aJoints.w];
  gl_Position = g.lightViewProj[pc.cascade] * (pc.model * (skin * vec4(aPos, 1.0)));
}
