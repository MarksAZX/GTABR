#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec2 iSize;    // metres
layout(location = 2) in vec2 iPivot;   // fraction of the quad (u from left, v from top) that maps to iPos
layout(location = 3) in vec4 iUV;      // u0 v0 u1 v1
layout(location = 4) in vec4 iTint;    // unorm
layout(location = 5) in float iExtra;  // reserved
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vTint;
layout(location = 2) out float vDist;
void main() {
  vec2 c = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));  // strip: (0,0)(1,0)(0,1)(1,1)
  vec3 right = g.camRight.xyz;
  vec3 up = g.camUp.xyz;
  vec3 wp = iPos + right * ((c.x - iPivot.x) * iSize.x) + up * ((iPivot.y - c.y) * iSize.y);
  vUV = vec2(mix(iUV.x, iUV.z, c.x), mix(iUV.y, iUV.w, c.y));
  vTint = iTint;
  vDist = length(wp - g.camPos.xyz);
  gl_Position = g.viewProj * vec4(wp, 1.0);
}
