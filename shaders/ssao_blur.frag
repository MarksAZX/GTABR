#version 450
// Depth-aware blur of the half-resolution AO (keeps contact darkening crisp on silhouettes).
layout(set = 1, binding = 0) uniform sampler2D uDepth;
layout(set = 1, binding = 1) uniform sampler2D uAo;
layout(push_constant) uniform PC {
  vec4 proj;
  vec4 cfg;    // z = 1/width, w = 1/height (of the AO target)
} pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
float linZ(float d) { return pc.proj.x * pc.proj.y / (pc.proj.y - d * (pc.proj.y - pc.proj.x)); }

void main() {
  float zc = linZ(texture(uDepth, vUV).r);
  vec2 sum = vec2(0.0);
  float wsum = 0.0;
  for (int y = -2; y <= 1; ++y)
    for (int x = -2; x <= 1; ++x) {
      vec2 o = (vec2(x, y) + 0.5) * pc.cfg.zw;
      float zs = linZ(texture(uDepth, vUV + o).r);
      float w = exp(-abs(zs - zc) / (0.06 * zc + 0.05));
      sum += texture(uAo, vUV + o).rg * w;
      wsum += w;
    }
  outColor = vec4(sum / max(wsum, 1e-4), 0.0, 1.0);
}
