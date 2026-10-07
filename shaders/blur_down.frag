#version 450
layout(set = 1, binding = 0) uniform sampler2D uSrc;
layout(push_constant) uniform PC { vec2 halfPixel; float offsetScale; float pad; } pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
// Dual Kawase downsample
void main() {
  vec2 hp = pc.halfPixel * pc.offsetScale;
  vec4 sum = texture(uSrc, vUV) * 4.0;
  sum += texture(uSrc, vUV - hp);
  sum += texture(uSrc, vUV + hp);
  sum += texture(uSrc, vUV + vec2(hp.x, -hp.y));
  sum += texture(uSrc, vUV - vec2(hp.x, -hp.y));
  outColor = vec4((sum / 8.0).rgb, 1.0);
}
