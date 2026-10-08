#version 450
layout(set = 1, binding = 0) uniform sampler2D uTex;
layout(push_constant) uniform PC { vec2 screen; float srgb; float pad; } pc;
layout(location = 0) in vec2 vLocal;
layout(location = 1) in vec2 vHalf;
layout(location = 2) in vec4 vUV;
layout(location = 3) in vec4 vColor;
layout(location = 4) in vec4 vColor2;
layout(location = 5) in vec4 vParams;
layout(location = 6) in vec2 vP3Kind;
layout(location = 7) in vec2 vT;
layout(location = 0) out vec4 outColor;

float sdRoundBox(vec2 p, vec2 b, float r) {
  r = min(r, min(b.x, b.y));
  vec2 q = abs(p) - b + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
  int kind = int(vP3Kind.y + 0.5);
  float radius = vParams.x;
  float d = sdRoundBox(vLocal, vHalf, radius);
  float cover = 1.0 - smoothstep(-0.7, 0.7, d);
  vec2 uv = mix(vUV.xy, vUV.zw, vT);
  vec4 col = vColor;

  if (kind == 0) {  // rounded rect with optional border (p0 = border px, color2 = border colour)
    float bw = vParams.y;
    if (bw > 0.0) {
      float inner = 1.0 - smoothstep(-0.7, 0.7, d + bw);
      col = mix(vColor2, vColor, inner);
      col.a = mix(vColor2.a, vColor.a, inner);
    }
    outColor = vec4(col.rgb, col.a * cover);
  } else if (kind == 1) {  // icon / image
    vec4 t = texture(uTex, uv);
    outColor = vec4(pow(max(t.rgb, vec3(0.0)), vec3(1.0 / 2.2)) * col.rgb, t.a * col.a * cover);
  } else if (kind == 2) {  // SDF text; p0 = outline thickness (sdf units), color2 = outline colour
    float s = texture(uTex, uv).r;
    float w = max(fwidth(s) * 0.7, 0.012);
    float fill = smoothstep(0.5 - w, 0.5 + w, s);
    float ol = vParams.y;
    if (ol > 0.0) {
      float o = smoothstep(0.5 - ol - w, 0.5 - ol + w, s);
      vec4 oc = vec4(vColor2.rgb, vColor2.a * o);
      float a = fill * col.a;
      vec3 rgb = mix(oc.rgb, col.rgb, fill);
      float aa = max(a, oc.a);
      outColor = vec4(rgb, aa);
    } else {
      outColor = vec4(col.rgb, col.a * fill);
    }
  } else if (kind == 3) {  // ring / arc: p0 = r_in, p1 = r_out, p2/p3 = angle range (rad, 0 = up, clockwise)
    float r = length(vLocal);
    float rin = vParams.y, rout = vParams.z;
    float ra = smoothstep(rin - 0.8, rin + 0.8, r) * (1.0 - smoothstep(rout - 0.8, rout + 0.8, r));
    float a0 = vParams.w, a1 = vP3Kind.x;
    float ang = atan(vLocal.x, -vLocal.y);
    float span = a1 - a0;
    float rel = mod(ang - a0, 6.2831853);
    float inSeg = 1.0;
    if (span < 6.2831) {
      float edge = min(rel, span - rel) * r;  // px distance to the nearest radial edge
      inSeg = (rel <= span) ? smoothstep(-0.8, 0.8, edge) : 0.0;
    }
    outColor = vec4(col.rgb, col.a * ra * inSeg);
  } else if (kind == 4) {  // minimap: uv = (cu, cv, scale, rotation) packed in vUV
    vec2 c = vUV.xy;
    float scale = vUV.z, rot = vUV.w;
    vec2 q = (vLocal / max(vHalf, vec2(1e-3)));
    float cs = cos(rot), sn = sin(rot);
    vec2 r = vec2(q.x * cs - q.y * sn, q.x * sn + q.y * cs);
    vec4 t = texture(uTex, c + r * 0.5 * scale);
    outColor = vec4(pow(max(t.rgb, vec3(0.0)), vec3(1.0 / 2.2)) * col.rgb, col.a * cover);
  } else if (kind == 5) {  // vertical gradient
    vec4 gcol = mix(vColor, vColor2, clamp(vT.y, 0.0, 1.0));
    outColor = vec4(gcol.rgb, gcol.a * cover);
  } else {  // 6: soft shadow around a rounded rect; p0 = blur px
    float bl = max(vParams.y, 1.0);
    float a = 1.0 - smoothstep(-bl * 0.35, bl, d);
    outColor = vec4(col.rgb, col.a * a * a);
  }
  if (outColor.a < 0.003) discard;
  if (pc.srgb > 0.5) outColor.rgb = pow(outColor.rgb, vec3(2.2));
}
