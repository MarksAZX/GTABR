#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(set = 0, binding = 1) uniform sampler2DArrayShadow uShadow;
layout(set = 1, binding = 0) uniform sampler2D uAlbedo;   // rgb albedo, a = paint mask (vehicles)
layout(set = 1, binding = 1) uniform sampler2D uNormal;   // tangent-space normal
layout(set = 1, binding = 2) uniform sampler2D uORM;      // glTF metallic-roughness: g = roughness, b = metallic
#include "shadowing.glsl"
#include "probes.glsl"
// tint.rgb = paint colour, tint.a = recolour amount; params: x = emissive (lights), y = clear coat, z = roughness scale, w = fade
layout(push_constant) uniform PC { mat4 model; vec4 tint; vec4 params; } pc;
layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vTangent;
layout(location = 3) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
  vec4 A = texture(uAlbedo, vUV);
  vec3 albedo = A.rgb;
  float paintMask = A.a < 0.999 ? (1.0 - A.a) : 0.0;
  if (pc.tint.a > 0.0 && paintMask > 0.01) {
    float lum = dot(albedo, vec3(0.299, 0.587, 0.114));
    vec3 repaint = pc.tint.rgb * (0.35 + 1.3 * lum);
    albedo = mix(albedo, repaint, paintMask * pc.tint.a);
  }
  // pedestrians: tint.a < 0 requests per-person clothing variation (tint.r = hue turn, g = saturation, b = value) that spares skin and hair
  if (pc.tint.a < -0.5) {
    vec3 c = albedo;
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
    float sat = (mx - mn) / max(mx, 1e-4);
    // warm hues (red -> yellow-orange) of moderate saturation are skin / hair / tan leather: never re-tinted
    bool skinLike = c.r >= c.g * 0.97 && c.g >= c.b * 0.88 && sat > 0.10 && sat < 0.85;
    float clothes = skinLike ? 0.0 : 1.0;
    float w = clothes * smoothstep(0.10, 0.28, sat);
    float cs = cos(pc.tint.r), sn = sin(pc.tint.r);
    // rotation about the grey axis (YIQ)
    const mat3 toYiq = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
    const mat3 fromYiq = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
    vec3 yiq = toYiq * c;
    yiq.yz = vec2(yiq.y * cs - yiq.z * sn, yiq.y * sn + yiq.z * cs) * pc.tint.g;
    vec3 shifted = max(fromYiq * yiq, vec3(0.0));
    albedo = mix(c, shifted, w);
    // brightness variety also for neutral garments (dark jeans vs grey, white vs cream)
    albedo *= mix(1.0, pc.tint.b, clothes * 0.85);
  }
  vec3 orm = texture(uORM, vUV).rgb;
  float rough = clamp(orm.g * pc.params.z, 0.05, 1.0);
  float metal = orm.b;
  vec3 Nn = normalize(vNormal);
  vec3 T = normalize(vTangent.xyz - Nn * dot(Nn, vTangent.xyz));
  vec3 B = cross(Nn, T) * vTangent.w;
  vec3 tn = texture(uNormal, vUV).xyz * 2.0 - 1.0;
  vec3 N = normalize(mat3(T, B, Nn) * tn);
  if (!gl_FrontFacing) N = -N;

  vec3 V = normalize(g.camPos.xyz - vWorld);
  vec3 L = g.sunDir.xyz;
  vec3 H = normalize(L + V);
  float NoL = max(dot(N, L), 0.0), NoV = max(dot(N, V), 1e-3), NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
  vec3 f0 = mix(vec3(0.04), albedo, metal);
  vec3 diff = albedo * (1.0 - metal);
  float a = rough * rough;
  float indoor = g.params.w;
  float sh = shadowTerm(vWorld) * (1.0 - indoor);
  vec3 spec = D_GGX(NoH, a) * V_SmithJointApprox(NoV, NoL, a) * F_Schlick(f0, VoH);
  vec3 direct = (diff / PI + spec) * g.sunColor.rgb * NoL * sh;
  // automotive clear coat: a second sharp lobe on painted panels
  float cc = pc.params.y * max(paintMask, 0.35);
  if (cc > 0.0) {
    float ca = 0.04 * 0.04;
    vec3 Fc = F_Schlick(vec3(0.04), VoH);
    direct += D_GGX(NoH, ca) * V_SmithJointApprox(NoV, NoL, ca) * Fc * g.sunColor.rgb * NoL * sh * cc;
  }
  float hemi = N.y * 0.5 + 0.5;
  float skyVis;
  vec3 irr = probeAmbient(vWorld + N * 0.3, N, skyVis);
  vec3 R = reflect(-V, N);
  float specVis;
  probeAmbient(vWorld + N * 0.3, R, specVis);
  vec3 env = skyRadiance(R, 0.0) * mix(0.15, 1.0, specVis * specVis);
  vec3 lamp = vec3(1.0, 0.94, 0.84) * (0.75 + 0.25 * N.y);
  irr = mix(irr, lamp, indoor);
  env = mix(env, lamp * 0.6, indoor);
  // crude specular occlusion toward the ground
  float so = clamp(0.6 + R.y, 0.25, 1.0);
  vec3 ambient = diff * irr + env * envBRDF(f0, rough, NoV) * so;
  if (cc > 0.0) ambient += env * envBRDF(vec3(0.04), 0.05, NoV) * cc * so;
  vec3 col = direct + ambient + evalLights(vWorld, N, V, diff, f0, rough);
  col += albedo * pc.params.x;
  col = applyFog(col, vWorld);
  outColor = vec4(col, pc.params.w);
}
