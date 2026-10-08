// Cascaded shadow lookup (2 cascades in a depth array, hardware compare + 5-tap PCF).
float shadowCascade(vec3 worldPos, int c) {
  vec4 sc = g.lightViewProj[c] * vec4(worldPos, 1.0);
  vec3 p = sc.xyz / sc.w;
  vec2 uv = p.xy * 0.5 + 0.5;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || p.z > 1.0) return 1.0;
  float t = g.params.y * (c == 0 ? 1.0 : 0.6);
  float s = texture(uShadow, vec4(uv, float(c), p.z)) * 0.28;
  s += texture(uShadow, vec4(uv + vec2(t, 0.0), float(c), p.z)) * 0.18;
  s += texture(uShadow, vec4(uv - vec2(t, 0.0), float(c), p.z)) * 0.18;
  s += texture(uShadow, vec4(uv + vec2(0.0, t), float(c), p.z)) * 0.18;
  s += texture(uShadow, vec4(uv - vec2(0.0, t), float(c), p.z)) * 0.18;
  if(g.effectsInfo.z>0.5){s*=0.52;
    s+=texture(uShadow,vec4(uv+vec2(t,t),float(c),p.z))*0.12;
    s+=texture(uShadow,vec4(uv+vec2(-t,t),float(c),p.z))*0.12;
    s+=texture(uShadow,vec4(uv+vec2(t,-t),float(c),p.z))*0.12;
    s+=texture(uShadow,vec4(uv-vec2(t,t),float(c),p.z))*0.12;
  }
  if (c == 0) return s;
  vec2 e = abs(uv - 0.5) * 2.0;
  return mix(s, 1.0, smoothstep(0.9, 1.0, max(e.x, e.y)));
}
// Cascade selection by coverage: the near cascade wherever the point lies inside it, blended into the far one at its rim.
float shadowTerm(vec3 worldPos) {
  if (g.sunDir.w <= 0.0) return 1.0;
  int count = int(g.cascade.y);
  vec4 c0 = g.lightViewProj[0] * vec4(worldPos, 1.0);
  vec2 e0 = abs(c0.xy / c0.w);
  float edge = max(e0.x, e0.y);
  float s;
  if (count < 2 || edge < 0.8) s = shadowCascade(worldPos, 0);
  else if (edge > 0.95) s = shadowCascade(worldPos, 1);
  else s = mix(shadowCascade(worldPos, 0), shadowCascade(worldPos, 1), smoothstep(0.8, 0.95, edge));
  return mix(1.0, s, g.sunDir.w);
}
