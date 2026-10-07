#version 450
layout(set = 1, binding = 0) uniform sampler2D uScene;
layout(set = 1, binding = 1) uniform sampler2D uBlur;
layout(push_constant) uniform PC {
  float blur;      // 0..1
  float fade;      // 0 = clear, 1 = black
  float vignette;
  float srgbTarget;
  float time;
  float grade;     // cinematic grade strength
  float dim;       // extra darkening when the radial wheel is open
  float pad;
} pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
void main() {
  vec3 s = texture(uScene, vUV).rgb;
  if (pc.blur > 0.001) s = mix(s, texture(uBlur, vUV).rgb, pc.blur);
  // gentle cinematic grade: lift teal in shadows, warm highlights
  float l = dot(s, vec3(0.299, 0.587, 0.114));
  vec3 graded = s + pc.grade * ((vec3(-0.012, 0.004, 0.018) * (1.0 - l)) + (vec3(0.02, 0.008, -0.012) * l));
  graded = mix(vec3(dot(graded, vec3(0.299, 0.587, 0.114))), graded, 1.0 + 0.06 * pc.grade);
  // vignette
  vec2 q = vUV - 0.5;
  float v = 1.0 - pc.vignette * smoothstep(0.35, 0.95, length(q * vec2(1.0, 1.15)));
  graded *= v;
  graded *= 1.0 - pc.dim * 0.45;
  graded = mix(graded, vec3(0.0), pc.fade);
  if (pc.srgbTarget > 0.5) graded = pow(graded, vec3(2.2));
  outColor = vec4(graded, 1.0);
}
