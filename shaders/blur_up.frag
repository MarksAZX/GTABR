#version 450
layout(set = 1, binding = 0) uniform sampler2D uSrc;
layout(push_constant) uniform PC { vec2 halfPixel; float offsetScale; float pad; } pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
// Dual Kawase upsample
void main() {
  vec2 hp = pc.halfPixel * pc.offsetScale;
  vec4 sum = texture(uSrc, vUV + vec2(-hp.x * 2.0, 0.0));
  sum += texture(uSrc, vUV + vec2(-hp.x, hp.y)) * 2.0;
  sum += texture(uSrc, vUV + vec2(0.0, hp.y * 2.0));
  sum += texture(uSrc, vUV + vec2(hp.x, hp.y)) * 2.0;
  sum += texture(uSrc, vUV + vec2(hp.x * 2.0, 0.0));
  sum += texture(uSrc, vUV + vec2(hp.x, -hp.y)) * 2.0;
  sum += texture(uSrc, vUV + vec2(0.0, -hp.y * 2.0));
  sum += texture(uSrc, vUV + vec2(-hp.x, -hp.y)) * 2.0;
  outColor = vec4((sum / 12.0).rgb, 1.0);
}
