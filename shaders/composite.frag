#version 450
layout(set = 1, binding = 0) uniform sampler2D uScene;
layout(set = 1, binding = 1) uniform sampler2D uBlur;
layout(set = 1, binding = 2) uniform sampler2D uAo;   // r = ambient visibility, g = sky mask (half resolution)
layout(push_constant) uniform PC {
  vec4 a;      // x = wheel/menu blur, y = fade to black, z = vignette, w = sRGB target
  vec4 b;      // x = exposure, y = bloom strength, z = bloom threshold, w = time
  vec4 lift;   // rgb lift (shadows), w = saturation
  vec4 gain;   // rgb gain (highlights), w = contrast
  vec4 fx;     // x = AO strength, y = light-shaft intensity, zw = sun position in uv
  vec4 sunCol; // rgb = shaft colour, w = unused
} pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// ACES fitted (Stephen Hill)
vec3 aces(vec3 v) {
  const mat3 i = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
  const mat3 o = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
  v = i * v;
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return clamp(o * (a / b), 0.0, 1.0);
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
  // lens: slight chromatic aberration towards the edges (cheap, only 2 extra taps of the scene)
  vec2 qc = vUV - 0.5;
  vec2 ca = qc * (0.0016 * dot(qc, qc) * 4.0);
  vec3 hdr = vec3(texture(uScene, vUV + ca).r, texture(uScene, vUV).g, texture(uScene, vUV - ca).b);
  // ambient occlusion: darkens crevices and contact areas, less on bright direct light (it only models ambient)
  float ao = texture(uAo, vUV).r;
  float lum0 = dot(hdr, vec3(0.2126, 0.7152, 0.0722));
  hdr *= mix(1.0, ao, pc.fx.x * (1.0 - smoothstep(1.4, 4.0, lum0) * 0.65));
  // volumetric light shafts: march from the pixel towards the sun and accumulate unoccluded sky
  if (pc.fx.y > 0.001) {
    vec2 toSun = pc.fx.zw - vUV;
    float dist = length(toSun);
    vec2 stepv = toSun / 20.0 * clamp(0.8 / max(dist, 0.8), 0.3, 1.0);
    vec2 uv = vUV + stepv * (hash(vUV * 811.0 + pc.b.w) * 0.9 + 0.1);
    float acc = 0.0, decay = 1.0;
    for (int i = 0; i < 20; ++i) {
      acc += texture(uAo, uv).g * decay;
      decay *= 0.93;
      uv += stepv;
    }
    float onScreen = 1.0 - smoothstep(0.9, 1.7, length(pc.fx.zw - 0.5) * 1.4);
    hdr += pc.sunCol.rgb * (acc / 20.0) * pc.fx.y * onScreen * smoothstep(1.6, 0.1, dist * 1.2);
    // soft glare halo around the sun disc (only where the sky is visible, so buildings and trees cut it)
    float skyHere = texture(uAo, pc.fx.zw).g;
    float halo = pow(clamp(1.0 - dist * 2.6, 0.0, 1.0), 3.0);
    hdr += pc.sunCol.rgb * halo * pc.fx.y * 0.35 * skyHere;
  }
  vec3 blurred = texture(uBlur, vUV).rgb;
  vec3 bloom = max(blurred - vec3(pc.b.z), 0.0) * pc.b.y;
  hdr = mix(hdr, blurred, pc.a.x) + bloom;
  vec3 c = aces(hdr * pc.b.x);
  // grading: lift / gain, contrast, saturation (display referred)
  c = c * pc.gain.rgb + pc.lift.rgb * (1.0 - c);
  c = clamp((c - 0.5) * pc.gain.w + 0.5, 0.0, 1.0);
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c = mix(vec3(l), c, pc.lift.w);
  // vignette + subtle grain
  vec2 q = vUV - 0.5;
  c *= 1.0 - pc.a.z * smoothstep(0.3, 0.95, length(q * vec2(1.0, 1.2)));
  c = mix(c, vec3(0.0), pc.a.y);
  c = clamp(c, 0.0, 1.0);
  // display encoding
  vec3 outc = pow(c, vec3(1.0 / 2.2));
  // film grain after encoding (never amplified by the gamma curve), stronger in mid tones, almost absent in the darks
  float gl = dot(outc, vec3(0.3, 0.59, 0.11));
  outc += (hash(vUV * 1000.0 + pc.b.w) - 0.5) * 0.020 * smoothstep(0.02, 0.35, gl);
  if (pc.a.w > 0.5) outc = clamp(c + (outc - pow(c, vec3(1.0 / 2.2))) * 0.6, 0.0, 1.0);   // sRGB swapchain encodes in hardware
  outColor = vec4(outc, 1.0);
}
