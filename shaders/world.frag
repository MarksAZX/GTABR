#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(set = 0, binding = 1) uniform sampler2DArrayShadow uShadow;
layout(set = 1, binding = 0) uniform sampler2DArray uMat;    // albedo
layout(set = 1, binding = 1) uniform sampler2DArray uMatN;   // rg = normal xy, b = roughness, a = cavity / AO
#include "shadowing.glsl"
layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vUVL;
layout(location = 3) in vec4 vColor;
layout(location = 4) in float vEmissive;
layout(location = 0) out vec4 outColor;

vec3 perturb(vec3 N, vec3 P, vec2 uv, vec3 tn) {
  // cotangent frame from screen-space derivatives (no stored tangents on the world mesh)
  vec3 dp1 = dFdx(P), dp2 = dFdy(P);
  vec2 du1 = dFdx(uv), du2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
  vec3 T = dp2perp * du1.x + dp1perp * du2.x;
  vec3 B = dp2perp * du1.y + dp1perp * du2.y;
  float invmax = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);
  mat3 TBN = mat3(T * invmax, B * invmax, N);
  return normalize(TBN * tn);
}

void main() {
  vec3 Ng = normalize(vNormal);
  vec4 tex = texture(uMat, vUVL);
  vec4 nr = texture(uMatN, vUVL);
  vec3 albedo = tex.rgb * vColor.rgb;
  vec3 tn = vec3(nr.rg * 2.0 - 1.0, 0.0);
  tn.z = sqrt(max(1.0 - dot(tn.xy, tn.xy), 0.0));
  vec3 N = perturb(Ng, vWorld, vUVL.xy, tn);
  float rough = clamp(nr.b, 0.04, 1.0);
  float ao = vColor.a * mix(1.0, nr.a, 0.85);
  // rain-darkened / wet ground: darker albedo, glossier on horizontal surfaces
  float wet = g.cascade.w * smoothstep(0.7, 0.95, Ng.y);
  albedo *= 1.0 - 0.35 * wet;
  rough = mix(rough, 0.12, wet * 0.85);

  vec3 V = normalize(g.camPos.xyz - vWorld);
  vec3 L = g.sunDir.xyz;
  vec3 H = normalize(L + V);
  float NoL = max(dot(N, L), 0.0), NoV = max(dot(N, V), 1e-3), NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
  vec3 f0 = vec3(0.04);
  float a = rough * rough;
  float indoor = g.params.w;
  float sh = shadowTerm(vWorld) * (1.0 - indoor);
  vec3 direct = (albedo / PI + D_GGX(NoH, a) * V_SmithJointApprox(NoV, NoL, a) * F_Schlick(f0, VoH)) * g.sunColor.rgb * NoL * sh;

  // ambient: hemisphere irradiance + analytic sky reflection
  float hemi = N.y * 0.5 + 0.5;
  vec3 irr = mix(g.ambGround.rgb, g.ambSky.rgb, hemi);
  vec3 R = reflect(-V, N);
  vec3 env = skyRadiance(R, 0.0) * (1.0 - 0.6 * a);
  vec3 lamp = vec3(1.0, 0.94, 0.84) * (0.75 + 0.25 * N.y);
  irr = mix(irr, lamp, indoor);
  env = mix(env, lamp * 0.6, indoor);
  vec3 ambient = (albedo * irr + env * envBRDF(f0, rough, NoV)) * ao;

  vec3 col = direct + ambient + evalLights(vWorld, N, V, albedo, f0, rough);
  col += albedo * vEmissive * g.params.z * 6.0;   // shop signs / lamps glow at night
  col = applyFog(col, vWorld);
  outColor = vec4(col, 1.0);
}
