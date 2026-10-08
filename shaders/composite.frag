#version 450
layout(set = 0, binding = 0, std140) uniform Globals {
  mat4 viewProj; mat4 view; mat4 invViewProj; mat4 lightViewProj[2];
  vec4 camPos, camRight, camUp, camFwd;
  vec4 sunDir, sunColor, ambSky, ambGround, fog, params, sky0, sky1, cascade, lightInfo, probeRect, probeInfo, lightGrid;
  mat4 prevViewProj;
  vec4 post;   // x = motion blur, y = contact shadows, z = sharpening
  vec4 taa;
  vec4 look;   // x = film grain, y = chromatic aberration, z = vignette scale, w = golden-hour grade
} g;
layout(set = 1, binding = 0) uniform sampler2D uScene;
layout(set = 1, binding = 1) uniform sampler2D uBlur;
layout(set = 1, binding = 2) uniform sampler2D uAo;
layout(set = 1, binding = 3) uniform sampler2D uDepth;   // scene depth (nearest)   // r = ambient visibility, g = sky mask (half resolution)
layout(push_constant) uniform PC {
  vec4 a;      // x = wheel/menu blur, y = fade to black, z = vignette, w = sRGB target
  vec4 b;      // x = exposure, y = bloom strength, z = bloom threshold, w = time
  vec4 lift;   // rgb lift (shadows), w = saturation
  vec4 gain;   // rgb gain (highlights), w = contrast
  vec4 fx;     // x = AO strength, y = light-shaft intensity, zw = sun position in uv
  vec4 sunCol; // rgb = shaft colour, w = unused
  vec4 proj;   // near, far, tan(fov/2)*aspect, tan(fov/2)
  vec4 refl;   // xyz = world up in the camera basis (x right, y down, z forward), w = wetness (0 = no reflections)
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
float linZ(float d) { return pc.proj.x * pc.proj.y / (pc.proj.y - d * (pc.proj.y - pc.proj.x)); }
vec3 viewPos(vec2 uv, float z) { return vec3((uv.x * 2.0 - 1.0) * pc.proj.z * z, (uv.y * 2.0 - 1.0) * pc.proj.w * z, z); }

// Screen-space reflection of the HDR scene on up-facing wet ground: march the mirrored view ray against the depth buffer.
vec3 wetReflection(vec2 uv, float d, vec3 P) {
  vec2 px = 1.0 / vec2(textureSize(uDepth, 0));
  vec3 pr = viewPos(uv + vec2(px.x, 0), linZ(texture(uDepth, uv + vec2(px.x, 0)).r));
  vec3 pl = viewPos(uv - vec2(px.x, 0), linZ(texture(uDepth, uv - vec2(px.x, 0)).r));
  vec3 pu = viewPos(uv + vec2(0, px.y), linZ(texture(uDepth, uv + vec2(0, px.y)).r));
  vec3 pd = viewPos(uv - vec2(0, px.y), linZ(texture(uDepth, uv - vec2(0, px.y)).r));
  vec3 dx = abs(pr.z - P.z) < abs(P.z - pl.z) ? pr - P : P - pl;
  vec3 dy = abs(pu.z - P.z) < abs(P.z - pd.z) ? pu - P : P - pd;
  vec3 N = normalize(cross(dy, dx));
  if (dot(N, -P) < 0.0) N = -N;
  float upness = dot(N, pc.refl.xyz);
  if (upness < 0.92) return vec3(0.0);
  vec3 I = normalize(P);
  vec3 R = reflect(I, N);
  if (R.z <= 0.02) return vec3(0.0);            // towards the camera: nothing on screen to reflect
  float fres = 0.04 + 0.96 * pow(1.0 - clamp(dot(-I, N), 0.0, 1.0), 5.0);
  float step0 = 0.35 + 0.05 * P.z;                // metres per step, growing with distance
  float jitter = hash(uv * vec2(1920.0, 1080.0) + pc.b.w);
  vec3 pos = P;
  float t = step0 * (0.4 + 0.6 * jitter);
  for (int i = 0; i < 22; ++i) {
    pos = P + R * t;
    if (pos.z < pc.proj.x) break;
    vec2 suv = vec2(pos.x / (pos.z * pc.proj.z), pos.y / (pos.z * pc.proj.w)) * 0.5 + 0.5;
    if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;
    float sd = texture(uDepth, suv).r;
    if (sd < 0.99999) {
      float sz = linZ(sd);
      float diff = pos.z - sz;
      if (diff > 0.0 && diff < 0.5 + 0.12 * pos.z) {
        vec3 col = texture(uScene, suv).rgb;
        vec2 e = abs(suv - 0.5) * 2.0;
        float edge = 1.0 - smoothstep(0.78, 1.0, max(e.x, e.y));
        float fade = 1.0 - float(i) / 22.0;
        return min(col, vec3(24.0)) * fres * edge * fade * pc.refl.w;
      }
    }
    t += step0 * (1.0 + 0.12 * float(i));
  }
  return vec3(0.0);
}

void main() {
  // lens: slight chromatic aberration towards the edges (cheap, only 2 extra taps of the scene)
  vec2 qc = vUV - 0.5;
  vec2 ca = qc * (0.0016 * dot(qc, qc) * 4.0) * g.look.y;
  vec3 hdr = vec3(texture(uScene, vUV + ca).r, texture(uScene, vUV).g, texture(uScene, vUV - ca).b);
  float d0 = texture(uDepth, vUV).r;
  // camera motion blur: reproject this pixel with the previous frame's camera and smear along the screen-space velocity
  if (g.post.x > 0.001) {
    vec4 clip = vec4(vUV * 2.0 - 1.0, d0, 1.0);
    vec4 wp = g.invViewProj * clip;
    wp /= wp.w;
    vec4 pc0 = g.prevViewProj * wp;
    vec2 prevUV = pc0.xy / pc0.w * 0.5 + 0.5;
    vec2 vel = (vUV - prevUV) * g.post.x;
    float vl = length(vel);
    if (vl > 0.022) vel *= 0.022 / vl;
    if (vl > 0.0008) {
      vec3 acc = hdr;
      float wsum = 1.0;
      for (int i = 1; i <= 7; ++i) {
        float t = float(i) / 7.0 - 0.5;
        vec2 suv = vUV + vel * t * 1.6 + vel * (hash(vUV * 733.0 + float(i)) - 0.5) * 0.15;
        acc += texture(uScene, suv).rgb;
        wsum += 1.0;
      }
      hdr = acc / wsum;
    }
  }
  // gentle sharpening (unsharp mask on the HDR scene, clamped so highlights do not ring)
  if (g.post.z > 0.001) {
    vec2 px = 1.0 / vec2(textureSize(uScene, 0));
    vec3 avg = (texture(uScene, vUV + vec2(px.x, 0)).rgb + texture(uScene, vUV - vec2(px.x, 0)).rgb + texture(uScene, vUV + vec2(0, px.y)).rgb + texture(uScene, vUV - vec2(0, px.y)).rgb) * 0.25;
    vec3 dlt = hdr - avg;
    hdr += clamp(dlt, -0.35 * hdr, 0.35 * hdr + 0.02) * g.post.z;
  }
  // contact shadows: a short march toward the sun in view space against the depth buffer grounds props, feet and wheels
  if (g.post.y > 0.001 && d0 < 0.99999 && g.sunDir.w > 0.01) {
    vec3 sv = mat3(g.view) * g.sunDir.xyz;
    vec3 L = normalize(vec3(sv.x, -sv.y, -sv.z));      // x right, y down, z forward (the convention of viewPos below)
    float z0 = linZ(d0);
    vec3 P0 = viewPos(vUV, z0);
    float occ = 0.0;
    float jit = hash(vUV * 977.0 + pc.b.w);
    for (int i = 0; i < 8; ++i) {
      float t = (float(i) + 0.3 + jit * 0.7) / 8.0 * (0.12 + 0.05 * z0);
      vec3 p = P0 + L * t;
      if (p.z < pc.proj.x) break;
      vec2 suv = vec2(p.x / (p.z * pc.proj.z), p.y / (p.z * pc.proj.w)) * 0.5 + 0.5;
      if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;
      float sd = texture(uDepth, suv).r;
      if (sd < 0.99999) {
        float diff = p.z - linZ(sd);
        if (diff > 0.012 * (1.0 + 0.3 * z0) && diff < 0.30 + 0.06 * z0) occ = max(occ, 1.0 - float(i) / 10.0);
      }
    }
    hdr *= 1.0 - occ * g.post.y * 0.55 * clamp(g.sunDir.w, 0.0, 1.0);
  }
  if (pc.refl.w > 0.02) {
    float dd = texture(uDepth, vUV).r;
    if (dd < 0.99999) hdr += wetReflection(vUV, dd, viewPos(vUV, linZ(dd)));
  }
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
  // colour style. Golden hour (cinema): teal-leaning shadows, warm amber highlights, a soft highlight bloom wash and a gentle
  // S-curve, the sun-drenched look of late afternoon on a coast. Vivid: more saturation and punch.
  if (g.look.w > 0.5 && g.look.w < 1.5) {
    float lw = dot(c, vec3(0.2126, 0.7152, 0.0722));
    vec3 shadowT = vec3(0.93, 1.00, 1.06), highT = vec3(1.06, 1.00, 0.90);
    c *= mix(shadowT, highT, smoothstep(0.08, 0.80, lw));
    c += vec3(1.0, 0.62, 0.32) * max(blurred - vec3(0.6), 0.0).r * 0.015 * pc.b.x;   // warm glow around bright areas
    c = clamp(c, 0.0, 1.0);
    c = c * c * (3.0 - 2.0 * c) * 0.22 + c * 0.78;                                       // filmic S-curve
    float l2 = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(l2), c, 1.10);
  } else if (g.look.w > 1.5) {
    float l2 = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(l2), c, 1.22);
    c = clamp((c - 0.5) * 1.06 + 0.5, 0.0, 1.0);
  }
  // vignette + subtle grain
  vec2 q = vUV - 0.5;
  c *= 1.0 - pc.a.z * g.look.z * smoothstep(0.3, 0.95, length(q * vec2(1.0, 1.2)));
  c = mix(c, vec3(0.0), pc.a.y);
  c = clamp(c, 0.0, 1.0);
  // display encoding
  vec3 outc = pow(c, vec3(1.0 / 2.2));
  // film grain after encoding (never amplified by the gamma curve), stronger in mid tones, almost absent in the darks
  float gl = dot(outc, vec3(0.3, 0.59, 0.11));
  outc += (hash(vUV * 1000.0 + pc.b.w) - 0.5) * 0.020 * smoothstep(0.02, 0.35, gl) * g.look.x;
  if (pc.a.w > 0.5) outc = clamp(c + (outc - pow(c, vec3(1.0 / 2.2))) * 0.6, 0.0, 1.0);   // sRGB swapchain encodes in hardware
  outColor = vec4(outc, 1.0);
}
