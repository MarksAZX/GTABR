#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(location = 0) in vec3 aPos;
void main() { gl_Position = g.lightViewProj * vec4(aPos, 1.0); }
