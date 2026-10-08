#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(push_constant) uniform PC { mat4 model; int cascade; } pc;
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aNormal;
void main(){vec4 p=pc.model*vec4(aPos,1);p.xyz+=foliageWind(p.xyz,max(aNormal.w,0),aPos.y);gl_Position=g.lightViewProj[pc.cascade]*p;}
