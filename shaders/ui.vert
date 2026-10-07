#version 450
layout(push_constant) uniform PC { vec2 screen; float srgb; float pad; } pc;
layout(location = 0) in vec4 iRect;    // x y w h in pixels
layout(location = 1) in vec4 iUV;
layout(location = 2) in vec4 iColor;   // unorm
layout(location = 3) in vec4 iColor2;  // unorm
layout(location = 4) in vec4 iParams;  // radius p0 p1 p2
layout(location = 5) in vec2 iP3Kind;  // p3, kind
layout(location = 0) out vec2 vLocal;
layout(location = 1) out vec2 vHalf;
layout(location = 2) out vec4 vUV;
layout(location = 3) out vec4 vColor;
layout(location = 4) out vec4 vColor2;
layout(location = 5) out vec4 vParams;
layout(location = 6) out vec2 vP3Kind;
layout(location = 7) out vec2 vT;
void main() {
  vec2 c = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
  vec2 half_ = iRect.zw * 0.5;
  float pad = 2.0;
  vec2 local = (c * 2.0 - 1.0) * (half_ + pad);
  vec2 center = iRect.xy + half_;
  vec2 p = center + local;
  vLocal = local;
  vHalf = half_;
  vUV = iUV;
  vColor = iColor;
  vColor2 = iColor2;
  vParams = iParams;
  vP3Kind = iP3Kind;
  vT = (local / max(half_, vec2(1e-3))) * 0.5 + 0.5;
  gl_Position = vec4(p / pc.screen * 2.0 - 1.0, 0.0, 1.0);
}
