#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(location = 0) in vec3 iPos;
layout(location = 1) in float iYaw;
layout(location = 2) in vec2 iHalf;   // half extents in metres (x across, z along)
layout(location = 3) in vec2 iAK;     // x = alpha, y = kind
layout(location = 0) out vec2 vP;
layout(location = 1) out vec2 vAK;
layout(location = 2) out vec3 vWorld;
layout(location = 3) out vec3 vSeed;
layout(location = 4) out vec3 vNormal;  // per-decal random numbers + metres per unit
void main() {
  vec2 c = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1)) * 2.0 - 1.0;
  vP = c;
  vAK = iAK;
  float s = sin(iYaw), co = cos(iYaw);
  vec2 local = c * iHalf;
  // local x -> object right, local y -> object forward (-z at yaw 0)
  vec3 right = vec3(co, 0.0, s);
  vec3 fwd = vec3(s, 0.0, -co);
  bool vertical = iAK.y >= 19.5;
  // ground decals lie flat (local y runs along the forward axis); wall decals stand up and face 'fwd'
  vec3 wp = vertical ? (iPos + right * local.x + vec3(0.0, local.y, 0.0)) : (iPos + right * local.x - fwd * local.y);
  vNormal = vertical ? fwd : vec3(0.0, 1.0, 0.0);
  vWorld = wp;
  vSeed = vec3(fract(sin(dot(iPos.xz, vec2(12.9898, 78.233))) * 43758.5453), fract(sin(dot(iPos.xz, vec2(39.346, 11.135))) * 24634.6345),
               max(iHalf.x, iHalf.y));
  gl_Position = g.viewProj * vec4(wp, 1.0);
}
