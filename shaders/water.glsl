// Ocean surface: a sum of directional (Gerstner-like) waves. uv.y of the sea mesh is the distance from the
// shoreline in metres, so waves grow offshore and break into foam on the beach.
float waveAmp(float shoreDist) { return mix(0.06, 0.42, smoothstep(0.0, 40.0, shoreDist)); }

// returns height; d = (dh/dx, dh/dz)
float waves(vec2 p, float t, float amp, out vec2 d) {
  const vec2 dirs[5] = vec2[5](vec2(0.92, 0.39), vec2(0.6, -0.8), vec2(-0.35, 0.94), vec2(0.99, -0.12), vec2(-0.7, -0.71));
  const float lens[5] = float[5](17.0, 9.5, 6.2, 3.7, 2.3);
  const float amps[5] = float[5](1.0, 0.55, 0.32, 0.16, 0.09);
  float h = 0.0;
  d = vec2(0.0);
  for (int i = 0; i < 5; ++i) {
    float k = 6.2831853 / lens[i];
    float w = sqrt(9.81 * k);
    float ph = k * dot(dirs[i], p) - w * t * 0.55 + float(i) * 1.7;
    float a = amps[i] * amp;
    h += a * sin(ph);
    d += a * k * cos(ph) * dirs[i];
  }
  return h;
}
