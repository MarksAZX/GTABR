#version 450
layout(location = 0) in vec2 vP;
layout(location = 1) in vec2 vAK;
layout(location = 0) out vec4 outColor;
float sdRoundBox(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
  float m;
  if (vAK.y < 0.5) {
    m = 1.0 - smoothstep(0.1, 1.0, length(vP));
    m *= m;
  } else {
    float d = sdRoundBox(vP, vec2(0.86), 0.34);
    m = 1.0 - smoothstep(-0.55, 0.12, d);
  }
  outColor = vec4(0.02, 0.025, 0.04, m * vAK.x);
}
