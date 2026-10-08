#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(set = 0, binding = 1) uniform sampler2DArrayShadow uShadow;
layout(set = 1, binding = 0) uniform sampler2D uAlbedo;   // rgb albedo, a = paint mask (vehicles)
layout(set = 1, binding = 1) uniform sampler2D uNormal;   // tangent-space normal
layout(set = 1, binding = 2) uniform sampler2D uORM;      // glTF metallic-roughness: g = roughness, b = metallic
#include "shadowing.glsl"
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
  if(pc.tint.a<-0.5){
    float lum=dot(albedo,vec3(0.299,0.587,0.114));
    bool skin=albedo.r>albedo.g*1.12&&albedo.g>albedo.b*1.05;
    if(!skin&&lum>0.07)albedo=mix(albedo,pc.tint.rgb*(0.35+lum*1.1),0.38);
  }
  vec3 orm = texture(uORM, vUV).rgb;
  float rough = clamp(orm.g * pc.params.z, 0.05, 1.0);
  float metal = orm.b;
  float wet=g.cascade.w*(1-g.params.w)*0.6;
  if(pc.tint.a>0)rough=mix(rough,0.13,wet);
  vec3 Nn = dot(vNormal,vNormal) > 0.00001 ? normalize(vNormal) : vec3(0,1,0);
  vec3 tangent = vTangent.xyz - Nn * dot(Nn, vTangent.xyz);
  if (dot(tangent,tangent)<0.00001) tangent=cross(Nn,abs(Nn.y)>0.9?vec3(0,0,1):vec3(0,1,0));
  vec3 T = normalize(tangent);
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
  vec3 irr = mix(g.ambGround.rgb, g.ambSky.rgb, hemi);
  vec3 R = reflect(-V, N);
  vec3 env = skyRadiance(R, 0.0);
  vec3 lamp = vec3(1.0, 0.94, 0.84) * (0.22 + 0.12 * N.y);
  irr = mix(irr, lamp, indoor);
  env = mix(env, lamp * 0.6, indoor);
  // crude specular occlusion toward the ground
  float so = clamp(0.6 + R.y, 0.25, 1.0);
  env *= g.reflectionInfo.x;
  vec3 ambient = diff * irr + env * envBRDF(f0, rough, NoV) * so;
  if (cc > 0.0) ambient += env * envBRDF(vec3(0.04), 0.05, NoV) * cc * so;
  vec3 col = direct + ambient + evalLights(vWorld, N, V, diff, f0, rough);
  col += albedo * pc.params.x;
  col = applyFog(col, vWorld);
  outColor = vec4(col, pc.params.w);
}
