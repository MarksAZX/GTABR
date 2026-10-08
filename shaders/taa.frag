#version 450
// Temporal anti-aliasing resolve: reproject last frame's resolved image with the depth buffer and both cameras, clamp it to the
// current 3x3 neighbourhood (variance clipping in YCoCg) and blend. The scene is rendered with a sub-pixel jitter that changes
// every frame, so the history accumulates real coverage samples: stable edges, no shimmer on thin wires, fences and foliage.
layout(set = 0, binding = 0, std140) uniform Globals {
  mat4 viewProj; mat4 view; mat4 invViewProj; mat4 lightViewProj[2];
  vec4 camPos, camRight, camUp, camFwd;
  vec4 sunDir, sunColor, ambSky, ambGround, fog, params, sky0, sky1, cascade, lightInfo, probeRect, probeInfo, lightGrid;
  mat4 prevViewProj;
  vec4 post;
  vec4 taa;
  vec4 look;
} g;
layout(set = 1, binding = 0) uniform sampler2D uCur;
layout(set = 1, binding = 1) uniform sampler2D uHist;
layout(set = 1, binding = 2) uniform sampler2D uDepth;
layout(push_constant) uniform PC { vec4 a; vec4 b; } pc;   // a.x = history weight, a.yz = texel size
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 toYCoCg(vec3 c) { return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25))); }
vec3 fromYCoCg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
// compress HDR so very bright pixels do not dominate the blend (fireflies / flicker)
vec3 tm(vec3 c) { return c / (1.0 + max(max(c.r, c.g), c.b)); }
vec3 itm(vec3 c) { return c / max(1e-4, 1.0 - max(max(c.r, c.g), c.b)); }

void main() {
  vec2 px = pc.a.yz;
  vec3 cur = texture(uCur, vUV).rgb;
  if (any(isnan(cur)) || any(isinf(cur))) cur = vec3(0.0);
  if (pc.a.x <= 0.0) { outColor = vec4(cur, 1.0); return; }
  // closest depth in a cross: edges reproject with the foreground's motion
  float d = texture(uDepth, vUV).r;
  vec2 dUV = vUV;
  for (int i = 0; i < 4; ++i) {
    vec2 o = vec2(i == 0 ? px.x : (i == 1 ? -px.x : 0.0), i == 2 ? px.y : (i == 3 ? -px.y : 0.0));
    float dd = texture(uDepth, vUV + o).r;
    if (dd < d) { d = dd; dUV = vUV + o; }
  }
  vec4 wp = g.invViewProj * vec4(dUV * 2.0 - 1.0, d, 1.0);
  wp /= wp.w;
  vec4 pcl = g.prevViewProj * wp;
  vec2 prevUV = vUV + (pcl.xy / pcl.w * 0.5 + 0.5 - dUV);
  // neighbourhood statistics
  vec3 m1 = vec3(0.0), m2 = vec3(0.0);
  vec3 cY = toYCoCg(tm(cur));
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
      vec3 s = toYCoCg(tm(texture(uCur, vUV + vec2(x, y) * px).rgb));
      m1 += s; m2 += s * s;
    }
  m1 /= 9.0; m2 /= 9.0;
  vec3 sigma = sqrt(max(m2 - m1 * m1, 0.0));
  vec3 lo = m1 - sigma * 1.15, hi = m1 + sigma * 1.15;
  // history (bilinear + light 5-tap sharpen to fight the softening of repeated resampling)
  vec3 h = tm(texture(uHist, prevUV).rgb);
  bool bad = any(isnan(h)) || any(isinf(h));
  vec3 hn = (tm(texture(uHist, prevUV + vec2(px.x, 0)).rgb) + tm(texture(uHist, prevUV - vec2(px.x, 0)).rgb) +
             tm(texture(uHist, prevUV + vec2(0, px.y)).rgb) + tm(texture(uHist, prevUV - vec2(0, px.y)).rgb)) * 0.25;
  h = max(h + (h - hn) * 0.35, 0.0);
  vec3 hY = toYCoCg(h);
  // clip toward the current colour (not just clamp): fewer ghosts on moving cars and characters
  vec3 dir = hY - m1;
  vec3 ext = max(abs(dir) / max(hi - m1, 1e-4), vec3(1.0));
  hY = m1 + dir / max(max(ext.x, ext.y), ext.z);
  float w = bad ? 0.0 : pc.a.x;
  if (bad) hY = cY;
  if (prevUV.x < 0.0 || prevUV.y < 0.0 || prevUV.x > 1.0 || prevUV.y > 1.0) w = 0.0;
  // fast motion: trust the current frame more
  float motion = length((prevUV - vUV) / px);
  w *= clamp(1.0 - motion * 0.02, 0.6, 1.0);
  vec3 res = fromYCoCg(mix(cY, hY, w));
  vec3 o = itm(clamp(res, 0.0, 0.999));
  if (any(isnan(o)) || any(isinf(o))) o = cur;
  outColor = vec4(o, 1.0);
}
