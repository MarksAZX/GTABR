#version 450
// Half-resolution screen-space ambient occlusion from the scene depth buffer. Output: R = ambient visibility, G = sky mask
// (used by the volumetric light shafts in the composite).
layout(set = 1, binding = 0) uniform sampler2D uDepth;
layout(push_constant) uniform PC {
  vec4 proj;   // x = near, y = far, z = tan(fov/2) * aspect, w = tan(fov/2)
  vec4 cfg;    // x = radius (m), y = intensity, z = 1/width, w = 1/height
} pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

float linZ(float d) { return pc.proj.x * pc.proj.y / (pc.proj.y - d * (pc.proj.y - pc.proj.x)); }
vec3 viewPos(vec2 uv, float z) { return vec3((uv.x * 2.0 - 1.0) * pc.proj.z * z, (uv.y * 2.0 - 1.0) * pc.proj.w * z, z); }
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
  float d0 = texture(uDepth, vUV).r;
  if (d0 >= 0.99999) { outColor = vec4(1.0, 1.0, 0.0, 1.0); return; }
  float z0 = linZ(d0);
  vec3 p0 = viewPos(vUV, z0);
  vec2 px = pc.cfg.zw;
  // normal from the smaller of the neighbouring depth differences (avoids smearing across silhouettes)
  vec3 pr = viewPos(vUV + vec2(px.x, 0), linZ(texture(uDepth, vUV + vec2(px.x, 0)).r));
  vec3 pl = viewPos(vUV - vec2(px.x, 0), linZ(texture(uDepth, vUV - vec2(px.x, 0)).r));
  vec3 pu = viewPos(vUV + vec2(0, px.y), linZ(texture(uDepth, vUV + vec2(0, px.y)).r));
  vec3 pd = viewPos(vUV - vec2(0, px.y), linZ(texture(uDepth, vUV - vec2(0, px.y)).r));
  vec3 dx = abs(pr.z - p0.z) < abs(p0.z - pl.z) ? pr - p0 : p0 - pl;
  vec3 dy = abs(pu.z - p0.z) < abs(p0.z - pd.z) ? pu - p0 : p0 - pd;
  vec3 n = normalize(cross(dy, dx));
  if (dot(n, -p0) < 0.0) n = -n;
  float rad = pc.cfg.x;
  // radius shrinks with distance so far geometry does not turn into a dark halo
  rad *= clamp(1.6 - z0 / 90.0, 0.5, 1.0);
  float ang = ign(gl_FragCoord.xy) * 6.2831853;
  float occ = 0.0;
  const int N = 12;
  for (int i = 0; i < N; ++i) {
    float fi = (float(i) + 0.5) / float(N);
    float a = ang + float(i) * 2.399963;           // golden angle spiral
    vec2 disk = vec2(cos(a), sin(a)) * sqrt(fi);
    // hemisphere sample around the normal
    vec3 t = normalize(abs(n.y) < 0.95 ? cross(n, vec3(0, 1, 0)) : cross(n, vec3(1, 0, 0)));
    vec3 b = cross(n, t);
    float hz = sqrt(max(0.0, 1.0 - dot(disk, disk)));
    vec3 s = p0 + (t * disk.x + b * disk.y + n * hz) * rad * mix(0.25, 1.0, fi);
    vec2 suv = vec2(s.x / (s.z * pc.proj.z), s.y / (s.z * pc.proj.w)) * 0.5 + 0.5;
    float sd = linZ(texture(uDepth, suv).r);
    float diff = s.z - sd;       // positive: something in front of the sample
    float range = smoothstep(0.0, 1.0, rad / max(abs(z0 - sd), 1e-3));
    occ += step(0.04 * z0 * 0.1 + 0.03, diff) * range;
  }
  float vis = 1.0 - clamp(occ / float(N) * pc.cfg.y, 0.0, 1.0);
  outColor = vec4(vis, 0.0, 0.0, 1.0);
}
