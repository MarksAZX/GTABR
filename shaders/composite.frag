#version 450
#extension GL_GOOGLE_include_directive : require
#include "globals.glsl"
layout(set = 1, binding = 0) uniform sampler2D uScene;
layout(set = 1, binding = 1) uniform sampler2D uBlur;
layout(set = 1, binding = 2) uniform sampler2D uDepth;
layout(push_constant) uniform PC {
  vec4 a;      // x = wheel/menu blur, y = fade to black, z = vignette, w = sRGB target
  vec4 b;      // x = exposure, y = bloom strength, z = bloom threshold, w = time
  vec4 lift;   // rgb lift (shadows), w = saturation
  vec4 gain;   // rgb gain (highlights), w = contrast
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

vec3 worldAt(vec2 uv,float depth){vec4 p=g.invViewProj*vec4(uv*2.0-1.0,depth,1);return p.xyz/p.w;}
vec3 coastalReflection(vec3 hdr){
  if(g.reflectionInfo.y<1)return hdr;
  float depth=texture(uDepth,vUV).r;if(depth>=0.99999)return hdr;
  vec3 p=worldAt(vUV,depth);
  if(p.y>g.reflectionInfo.w+0.45||p.y<g.reflectionInfo.w-0.45 || p.x<g.waterBounds.x || p.x>g.waterBounds.z || p.z<g.waterBounds.y || p.z>g.waterBounds.w)return hdr;
  vec3 V=normalize(g.camPos.xyz-p),R=reflect(-V,vec3(0,1,0));
  if(R.y<=0.01)return hdr;
  int count=int(g.reflectionInfo.y);float previous=-1;
  for(int i=1;i<=28;++i){if(i>count)break;
    float travel=0.45+float(i)*float(i)*0.075;vec3 ray=p+R*travel;
    vec4 clip=g.viewProj*vec4(ray,1);if(clip.w<=0)break;
    vec2 uv=clip.xy/clip.w*0.5+0.5;if(any(lessThan(uv,vec2(0.015)))||any(greaterThan(uv,vec2(0.985))))break;
    float sampled=texture(uDepth,uv).r;if(sampled>=0.99999){previous=-1;continue;}
    vec3 surface=worldAt(uv,sampled);
    float delta=-(g.view*vec4(ray,1)).z+(g.view*vec4(surface,1)).z;
    if(delta>=0 && delta<0.3+travel*0.035 && previous<0 && surface.y>g.reflectionInfo.w+0.65){
      float edge=smoothstep(0.01,0.09,min(min(uv.x,uv.y),min(1-uv.x,1-uv.y)));
      float fresnel=0.06+0.65*pow(1-max(V.y,0),5);
      return mix(hdr,texture(uScene,uv).rgb,edge*fresnel*0.65);
    }
    previous=delta;
  }
  return hdr; // the world shader already supplies the sky fallback for missing/off-screen hits
}
float contactAO(){
  if(g.effectsInfo.x<0.01)return 1;
  float depth=texture(uDepth,vUV).r;
  vec3 wp=worldAt(vUV,depth);vec3 p=(g.view*vec4(wp,1)).xyz;vec3 n=cross(dFdx(p),dFdy(p));
  if(depth>=0.99999)return 1;
  if(wp.y<g.reflectionInfo.w+0.45&&wp.y>g.reflectionInfo.w-0.45&&wp.x>g.waterBounds.x&&wp.x<g.waterBounds.z&&wp.z>g.waterBounds.y&&wp.z<g.waterBounds.w)return 1;
  if(dot(n,n)<0.000001)return 1;n=normalize(n);if(dot(n,-p)<0)n=-n;
  vec2 scale=vec2(textureSize(uDepth,0));float radius=clamp(0.65/max(-p.z,1),0.002,0.035);
  float occ=0;int count=int(g.effectsInfo.y);
  for(int i=0;i<8;++i){if(i>=count)break;float angle=float(i)*2.39996;
    vec2 uv=vUV+vec2(cos(angle),sin(angle))*radius*(0.4+0.6*float(i+1)/float(count))*vec2(scale.y/scale.x,1);
    if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))continue;
    float z=texture(uDepth,uv).r;if(z>=0.99999)continue;
    vec3 q=(g.view*vec4(worldAt(uv,z),1)).xyz-p;float distance=length(q);
    occ+=smoothstep(0.03,0.22,dot(n,q))*(1-smoothstep(0.3,1.2,distance));
  }
  return 1-g.effectsInfo.x*occ/float(count);
}
void main() {
  vec3 hdr = coastalReflection(texture(uScene, vUV).rgb)*contactAO();
  vec3 blurred = hdr;
  if (pc.a.x > 0.001 || pc.b.y > 0.001) blurred = texture(uBlur, vUV).rgb;
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
  c += (hash(vUV * 1000.0 + pc.b.w) - 0.5) * 0.003;
  c = mix(c, vec3(0.0), pc.a.y);
  c = clamp(c, 0.0, 1.0);
  // display encoding
  vec3 outc = pow(c, vec3(1.0 / 2.2));
  if (pc.a.w > 0.5) outc = c;   // sRGB swapchain encodes in hardware (c is linear-ish display value)
  outColor = vec4(outc, 1.0);
}
