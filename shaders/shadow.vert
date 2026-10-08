#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(push_constant) uniform PC { mat4 model; int cascade; } pc;
layout(location = 0) in vec3 aPos;
void main() { gl_Position = g.lightViewProj[pc.cascade] * (pc.model * vec4(aPos, 1.0)); }
