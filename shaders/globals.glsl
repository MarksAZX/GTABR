// Shared per-frame uniform block (set 0, binding 0) and tone mapping helpers.
layout(set = 0, binding = 0, std140) uniform Globals {
  mat4 viewProj;
  mat4 view;
  mat4 invViewProj;
  mat4 lightViewProj;
  vec4 camPos;     // xyz, w = time (s)
  vec4 camRight;   // xyz
  vec4 camUp;      // xyz
  vec4 camFwd;     // xyz
  vec4 sunDir;     // xyz = direction TOWARDS the sun, w = shadow strength (0..1)
  vec4 sunColor;   // rgb
  vec4 ambSky;     // rgb
  vec4 ambGround;  // rgb
  vec4 fog;        // rgb color, w = density
  vec4 params;     // x = exposure, y = shadow texel size, z = night factor, w = unused
} g;

vec3 tonemapACES(vec3 x) {
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
// Scene shaders output display-referred (gamma) colour.
vec3 encodeDisplay(vec3 lin) { return pow(tonemapACES(lin * g.params.x), vec3(1.0 / 2.2)); }
