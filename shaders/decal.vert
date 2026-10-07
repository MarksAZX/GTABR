#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(location = 0) in vec3 iPos;
layout(location = 1) in float iYaw;
layout(location = 2) in vec2 iHalf;   // half extents in metres (x across, z along)
layout(location = 3) in vec2 iAK;     // x = alpha, y = kind
layout(location = 0) out vec2 vP;
layout(location = 1) out vec2 vAK;
void main() {
  vec2 c = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1)) * 2.0 - 1.0;
  vP = c;
  vAK = iAK;
  float s = sin(iYaw), co = cos(iYaw);
  vec2 local = c * iHalf;
  // local x -> object right, local y -> object forward (-z at yaw 0)
  vec3 right = vec3(co, 0.0, s);
  vec3 fwd = vec3(s, 0.0, -co);
  vec3 wp = iPos + right * local.x - fwd * local.y;
  gl_Position = g.viewProj * vec4(wp, 1.0);
}
