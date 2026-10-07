// Procedural vehicle models (compact hatchback, sedan, pickup) built by lofting squircle cross-sections.
#include <cmath>
#include <functional>

#include "models.h"

namespace bake {

std::vector<Material> makeMaterials() {
  std::vector<Material> m(M_COUNT);
  m[M_PAINT] = {{0.8f, 0.8f, 0.8f}, 0.75f, 110.0f, 0.42f, 0.0f, 0.78f, true};
  m[M_GLASS] = {{0.045f, 0.065f, 0.085f}, 1.0f, 160.0f, 0.95f, 0.0f, 0.9f, false};
  m[M_PLASTIC] = {{0.07f, 0.07f, 0.075f}, 0.18f, 22.0f, 0.04f, 0.0f, 0.7f, false};
  m[M_CHROME] = {{0.78f, 0.79f, 0.82f}, 0.9f, 90.0f, 0.55f, 0.0f, 0.85f, false};
  m[M_TIRE] = {{0.028f, 0.028f, 0.03f}, 0.06f, 14.0f, 0.0f, 0.0f, 0.6f, false};
  m[M_LIGHT_F] = {{1.0f, 0.93f, 0.75f}, 0.9f, 80.0f, 0.4f, 0.55f, 1.0f, false};
  m[M_LIGHT_R] = {{0.85f, 0.04f, 0.04f}, 0.8f, 60.0f, 0.3f, 0.5f, 1.0f, false};
  m[M_PLATE] = {{0.92f, 0.92f, 0.9f}, 0.2f, 20.0f, 0.0f, 0.0f, 1.0f, false};
  m[M_SKIN] = {{0.8f, 0.6f, 0.45f}, 0.12f, 18.0f, 0.0f, 0.0f, 0.8f, true};
  m[M_CLOTH] = {{0.5f, 0.5f, 0.5f}, 0.03f, 10.0f, 0.0f, 0.0f, 0.7f, true};
  m[M_HAIR] = {{0.06f, 0.05f, 0.04f}, 0.28f, 38.0f, 0.0f, 0.0f, 0.85f, true};
  m[M_SHOE] = {{0.9f, 0.9f, 0.9f}, 0.12f, 22.0f, 0.0f, 0.0f, 0.55f, true};
  m[M_FOLIAGE] = {{0.2f, 0.4f, 0.12f}, 0.1f, 18.0f, 0.0f, 0.0f, 1.0f, true};
  m[M_BARK] = {{0.25f, 0.18f, 0.12f}, 0.03f, 8.0f, 0.0f, 0.0f, 0.8f, true};
  m[M_METAL] = {{0.5f, 0.52f, 0.55f}, 0.5f, 50.0f, 0.25f, 0.0f, 0.8f, true};
  m[M_RUBBER] = {{0.05f, 0.05f, 0.05f}, 0.05f, 12.0f, 0.0f, 0.0f, 0.7f, true};
  m[M_MATTE] = {{0.5f, 0.5f, 0.5f}, 0.05f, 10.0f, 0.0f, 0.0f, 0.8f, true};
  return m;
}

namespace {
struct Station { float z, yb, yt, hwb, hwt, n; };

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Smooth interpolation through (z, v) keypoints (monotone in z).
float curve(const std::vector<std::pair<float, float>>& k, float z) {
  if (z <= k.front().first) return k.front().second;
  if (z >= k.back().first) return k.back().second;
  for (size_t i = 0; i + 1 < k.size(); ++i)
    if (z <= k[i + 1].first) {
      float t = (z - k[i].first) / std::max(1e-6f, k[i + 1].first - k[i].first);
      t = t * t * (3 - 2 * t);
      return lerpf(k[i].second, k[i + 1].second, t);
    }
  return k.back().second;
}

struct LoftOut { Mesh mesh; };

Mesh loft(const std::vector<Station>& st, int ringN, const std::function<uint16_t(Vec3, Vec3)>& matFn,
          const std::function<Vec3(Vec3, uint16_t)>& colFn, bool capFront, bool capBack,
          const std::function<float(Vec3, Vec3)>& auxFn = nullptr, uint16_t mat2 = 0xFFFF) {
  Mesh m;
  auto ringPoint = [&](const Station& s, int k) {
    float th = (float)k / ringN * 2 * kPi;
    float c = std::cos(th), sn = std::sin(th);
    float e = 2.0f / s.n;
    float sx = (c < 0 ? -1.f : 1.f) * std::pow(std::fabs(c), e), sy = (sn < 0 ? -1.f : 1.f) * std::pow(std::fabs(sn), e);
    float t = (sy + 1) * 0.5f;
    float hw = lerpf(s.hwb, s.hwt, t);
    return Vec3{hw * sx, (s.yb + s.yt) * 0.5f + (s.yt - s.yb) * 0.5f * sy, s.z};
  };
  std::vector<Vec3> pos;
  for (auto& s : st)
    for (int k = 0; k < ringN; ++k) pos.push_back(ringPoint(s, k));
  std::vector<Vec3> nrm(pos.size(), Vec3{0, 0, 0});
  struct T { uint32_t a, b, c; };
  std::vector<T> tris;
  int S = (int)st.size();
  for (int i = 0; i + 1 < S; ++i)
    for (int k = 0; k < ringN; ++k) {
      uint32_t a = i * ringN + k, b = i * ringN + (k + 1) % ringN, c = (i + 1) * ringN + k, d = (i + 1) * ringN + (k + 1) % ringN;
      tris.push_back({a, b, c});
      tris.push_back({b, d, c});
    }
  // orient outward: the ring winds counter-clockwise seen from +z; use centroid test
  for (auto& t : tris) {
    Vec3 e1 = pos[t.b] - pos[t.a], e2 = pos[t.c] - pos[t.a];
    Vec3 fn = e1.cross(e2);
    float len = fn.length();
    if (len < 1e-9f) continue;
    // outward direction approximated from the ring centre
    int si = t.a / ringN;
    Vec3 ctr{0, (st[si].yb + st[si].yt) * 0.5f, st[si].z};
    Vec3 cen = (pos[t.a] + pos[t.b] + pos[t.c]) / 3.0f;
    Vec3 out = cen - ctr;
    out.z = 0;
    if (fn.dot(out) < 0 && out.length() > 1e-5f) { std::swap(t.b, t.c); fn = fn * -1.0f; }
    nrm[t.a] += fn; nrm[t.b] += fn; nrm[t.c] += fn;
  }
  for (auto& n : nrm) n = n.normalized();
  m.v.reserve(pos.size());
  for (size_t i = 0; i < pos.size(); ++i) m.v.push_back({pos[i], nrm[i], Vec3{1, 1, 1}});
  for (auto& t : tris) {
    Vec3 e1 = pos[t.b] - pos[t.a], e2 = pos[t.c] - pos[t.a];
    Vec3 fn = e1.cross(e2);
    if (fn.length() < 1e-9f) continue;
    Vec3 cen = (pos[t.a] + pos[t.b] + pos[t.c]) / 3.0f;
    uint16_t mat = matFn(cen, fn.normalized());
    m.t.push_back({t.a, t.b, t.c, mat, mat2});
  }
  auto cap = [&](int si, float nz) {
    uint32_t base = (uint32_t)m.v.size();
    Vec3 ctr{0, (st[si].yb + st[si].yt) * 0.5f, st[si].z};
    m.v.push_back({ctr, {0, 0, nz}, Vec3{1, 1, 1}});
    for (int k = 0; k < ringN; ++k) m.v.push_back({pos[si * ringN + k], {0, 0, nz}, Vec3{1, 1, 1}});
    for (int k = 0; k < ringN; ++k) {
      uint32_t a = base + 1 + k, b = base + 1 + (k + 1) % ringN;
      Vec3 cen = (m.v[base].p + m.v[a].p + m.v[b].p) / 3.0f;
      uint16_t mat = matFn(cen, {0, 0, nz});
      Vec3 fn = (m.v[a].p - m.v[base].p).cross(m.v[b].p - m.v[base].p);
      if ((fn.z > 0) != (nz > 0)) m.t.push_back({base, b, a, mat});
      else m.t.push_back({base, a, b, mat});
    }
  };
  if (capFront) cap(0, -1.0f);
  if (capBack) cap(S - 1, 1.0f);
  for (auto& v : m.v) v.c = colFn(v.p, M_PAINT);
  if (auxFn) {
    for (size_t i = 0; i < pos.size(); ++i) m.v[i].aux = auxFn(m.v[i].p, m.v[i].n);
  }
  // per-triangle colour for non-paint materials is handled by the material albedo; vertex colour keeps paint tint
  return m;
}

struct CarDef {
  float L, W;
  float axleF, axleR, wheelR, wheelW, track;
  std::vector<std::pair<float, float>> hoodTop;  // lower body top height along z
  float cabZ0, cabZ1, cabZ2, cabZ3;              // windshield base, roof front, roof rear, rear window base
  float cabY0, cabRoof, cabY3;                   // base heights at windshield / roof / rear window base
  float beltY;
  float cabHwB, cabHwT;
  bool pickup;
  float bedZ0;
  int doors;
  float pillarB;
};

CarDef defFor(const std::string& n) {
  CarDef d{};
  if (n == "compacto") {
    d.L = 3.90f; d.W = 1.66f; d.axleF = -1.18f; d.axleR = 1.18f; d.wheelR = 0.285f; d.wheelW = 0.185f; d.track = 0.70f;
    d.hoodTop = {{-1.95f, 0.56f}, {-1.82f, 0.68f}, {-1.55f, 0.78f}, {-1.05f, 0.84f}, {-0.55f, 0.90f}, {1.0f, 0.95f}, {1.7f, 0.99f}, {1.88f, 0.94f}, {1.95f, 0.80f}};
    d.cabZ0 = -0.62f; d.cabZ1 = 0.05f; d.cabZ2 = 0.98f; d.cabZ3 = 1.70f; d.cabY0 = 0.90f; d.cabRoof = 1.45f; d.cabY3 = 0.98f;
    d.beltY = 0.90f; d.cabHwB = 0.76f; d.cabHwT = 0.60f; d.pickup = false; d.doors = 4; d.pillarB = 0.15f;
  } else if (n == "sedan") {
    d.L = 4.55f; d.W = 1.78f; d.axleF = -1.42f; d.axleR = 1.38f; d.wheelR = 0.31f; d.wheelW = 0.215f; d.track = 0.75f;
    d.hoodTop = {{-2.275f, 0.58f}, {-2.12f, 0.70f}, {-1.80f, 0.80f}, {-1.2f, 0.88f}, {-0.78f, 0.95f}, {0.9f, 0.97f}, {1.4f, 1.0f}, {2.1f, 1.02f}, {2.24f, 0.96f}, {2.275f, 0.84f}};
    d.cabZ0 = -0.82f; d.cabZ1 = -0.12f; d.cabZ2 = 0.62f; d.cabZ3 = 1.28f; d.cabY0 = 0.95f; d.cabRoof = 1.46f; d.cabY3 = 1.0f;
    d.beltY = 0.95f; d.cabHwB = 0.82f; d.cabHwT = 0.65f; d.pickup = false; d.doors = 4; d.pillarB = 0.14f;
  } else {  // picape
    d.L = 4.95f; d.W = 1.82f; d.axleF = -1.55f; d.axleR = 1.45f; d.wheelR = 0.34f; d.wheelW = 0.23f; d.track = 0.76f;
    d.hoodTop = {{-2.475f, 0.70f}, {-2.32f, 0.84f}, {-1.95f, 0.96f}, {-1.4f, 1.04f}, {-0.95f, 1.08f}, {0.7f, 1.08f}, {2.45f, 1.06f}, {2.475f, 0.96f}};
    d.cabZ0 = -0.98f; d.cabZ1 = -0.38f; d.cabZ2 = 0.30f; d.cabZ3 = 0.68f; d.cabY0 = 1.08f; d.cabRoof = 1.78f; d.cabY3 = 1.32f;
    d.beltY = 1.08f; d.cabHwB = 0.82f; d.cabHwT = 0.70f; d.pickup = true; d.bedZ0 = 0.78f; d.doors = 4; d.pillarB = 0.14f;
  }
  return d;
}
}  // namespace

Mesh buildVehicle(const std::string& model, const Vec3& paint) {
  CarDef d = defFor(model);
  Mesh car;
  const float halfL = d.L * 0.5f;
  const float bodyBottom = 0.22f;

  // --- lower body ---
  std::vector<Station> st;
  int nst = 46;
  float bodyEnd = d.pickup ? d.bedZ0 : halfL;
  (void)bodyEnd;
  for (int i = 0; i < nst; ++i) {
    float t = (float)i / (nst - 1);
    float z = -halfL + t * d.L;
    float top = curve(d.hoodTop, z);
    float ez = std::fabs(z) / halfL;
    float taper = 1.0f - 0.13f * std::pow(ez, 5.0f);
    float hw = d.W * 0.5f * taper;
    float bot = bodyBottom + 0.05f * std::pow(ez, 4.0f);
    if (d.pickup && z > d.bedZ0 - 0.02f) top = 0.99f;  // bed side height
    st.push_back({z, bot, top, hw, hw * 0.93f, 3.4f});
  }
  auto archF = [&](Vec3 c) {  // positive inside a wheel arch (metres)
    float best = -1e9f;
    for (float az : {d.axleF, d.axleR}) {
      float dz = c.z - az, dy = c.y - d.wheelR;
      float f = d.wheelR * 1.22f - std::sqrt(dz * dz + dy * dy);
      float fx = std::fabs(c.x) - d.W * 0.5f * 0.60f;
      best = std::max(best, std::min(f, fx));
    }
    return best;
  };
  auto bodyMat = [&](Vec3 c, Vec3 n) -> uint16_t { (void)c; (void)n; return M_PAINT; };
  auto bodyAux = [&](Vec3 p, Vec3 n) -> float {
    float f = archF(p);
    float sill = (std::fabs(n.y) < 0.85f) ? (0.37f - p.y) : -1.0f;
    float bump = std::min(std::fabs(p.z) - (halfL - 0.32f), 0.52f - p.y);
    f = std::max(f, std::max(sill, bump));
    return gtabr::clamp(0.5f + f * 14.0f, 0.0f, 1.0f);
  };
  auto paintCol = [&](Vec3 p, uint16_t) { (void)p; return paint; };
  Mesh body = loft(st, 32, bodyMat, paintCol, true, true, bodyAux, M_PLASTIC);
  // pickup bed: carve by painting the top of the bed region darker (open cargo liner)
  car.append(body);

  // --- cabin ---
  std::vector<Station> cs;
  int ncs = 40;
  float cz0 = d.cabZ0, cz3 = d.cabZ3;
  std::vector<std::pair<float, float>> roofK = {{d.cabZ0, d.cabY0}, {d.cabZ1, d.cabRoof}, {d.cabZ2, d.cabRoof}, {d.cabZ3, d.cabY3}};
  for (int i = 0; i < ncs; ++i) {
    float t = (float)i / (ncs - 1);
    float z = lerpf(cz0, cz3, t);
    float top;
    if (z < d.cabZ1) { float u = (z - d.cabZ0) / (d.cabZ1 - d.cabZ0); top = lerpf(d.cabY0, d.cabRoof, std::pow(u, 0.85f)); }
    else if (z <= d.cabZ2) top = d.cabRoof;
    else { float u = (z - d.cabZ2) / (d.cabZ3 - d.cabZ2); top = lerpf(d.cabRoof, d.cabY3, std::pow(u, 1.25f)); }
    // gentle roof crown
    float crown = (top > d.cabRoof - 0.01f) ? 0.0f : 0.0f;
    top += crown;
    float bot = d.cabY0 - 0.04f;
    if (top < bot + 0.002f) top = bot + 0.002f;
    float shrink = 1.0f;
    if (z < d.cabZ0 + 0.1f) shrink = 0.9f + 0.1f * (z - d.cabZ0) / 0.1f;
    cs.push_back({z, bot, top, d.cabHwB * shrink, d.cabHwT * shrink * (top - bot < 0.1f ? 1.0f : 1.0f), 3.2f});
  }
  float pillarA = 0.07f, pillarC = 0.07f;
  auto cabMat = [&](Vec3 c, Vec3 n) -> uint16_t { (void)c; (void)n; return M_PAINT; };
  auto cabAux = [&](Vec3 p, Vec3 n) -> float {
    float roofAt = d.cabRoof;
    if (p.z < d.cabZ1) roofAt = lerpf(d.cabY0, d.cabRoof, std::max(0.0f, (p.z - d.cabZ0) / (d.cabZ1 - d.cabZ0)));
    if (p.z > d.cabZ2) roofAt = lerpf(d.cabRoof, d.cabY3, std::min(1.0f, (p.z - d.cabZ2) / (d.cabZ3 - d.cabZ2)));
    float xm = d.cabHwT - 0.075f - std::fabs(p.x);
    float fW = std::min(std::min((-n.z - 0.30f) * 0.5f, xm), std::min(p.z - (d.cabZ0 + 0.03f), (d.cabZ1 - 0.03f) - p.z));
    float fR = std::min(std::min((n.z - 0.30f) * 0.5f, xm), std::min(p.z - (d.cabZ2 + 0.03f), (d.cabZ3 - 0.03f) - p.z));
    float zA = d.cabZ0 + pillarA + 0.22f, zC = d.pickup ? d.cabZ3 - 0.12f : d.cabZ3 - pillarC - 0.18f;
    float zB = (zA + zC) * 0.5f + 0.04f;
    float fS = std::min(std::min((std::fabs(n.x) - 0.5f) * 0.4f, p.y - (d.cabY0 + 0.07f)), std::min(roofAt - 0.07f - p.y, std::min(p.z - zA, zC - p.z)));
    fS = std::min(fS, std::fabs(p.z - zB) - d.pillarB * 0.5f);
    float f = std::max(fW, std::max(fR, fS));
    return gtabr::clamp(0.5f + f * 16.0f, 0.0f, 1.0f);
  };
  Mesh cab = loft(cs, 28, cabMat, paintCol, false, false, cabAux, M_GLASS);
  car.append(cab);
  // close the cabin rear for the pickup: back wall of the cab
  if (d.pickup) {
    Mesh wall = makeRoundedBox({0, (d.cabY0 + d.cabY3) * 0.5f - 0.04f, d.cabZ3 + 0.01f}, {d.cabHwB * 2 - 0.05f, d.cabY3 - d.cabY0 + 0.1f, 0.06f}, 0.02f, 3, paint, M_PAINT);
    car.append(wall);
    // bed: interior floor, liner and side walls (dark plastic liner) + tailgate
    float bz0 = d.bedZ0, bz1 = halfL - 0.04f;
    Mesh floor = makeRoundedBox({0, 0.80f, (bz0 + bz1) * 0.5f}, {d.W - 0.22f, 0.04f, bz1 - bz0}, 0.01f, 2, Vec3{0.06f, 0.06f, 0.065f}, M_PLASTIC);
    car.append(floor);
    for (float sx : {-1.0f, 1.0f}) {
      Mesh liner = makeRoundedBox({sx * (d.W * 0.5f - 0.09f), 0.92f, (bz0 + bz1) * 0.5f}, {0.04f, 0.22f, bz1 - bz0 - 0.08f}, 0.01f, 2, Vec3{0.06f, 0.06f, 0.065f}, M_PLASTIC);
      car.append(liner);
    }
    Mesh front = makeRoundedBox({0, 0.92f, bz0 + 0.04f}, {d.W - 0.2f, 0.22f, 0.04f}, 0.01f, 2, Vec3{0.06f, 0.06f, 0.065f}, M_PLASTIC);
    car.append(front);
    Mesh tail = makeRoundedBox({0, 0.9f, bz1 + 0.02f}, {d.W - 0.1f, 0.2f, 0.05f}, 0.02f, 2, paint, M_PAINT);
    car.append(tail);
  }

  // --- wheels ---
  for (float az : {d.axleF, d.axleR})
    for (float sx : {-1.0f, 1.0f}) {
      float x = sx * d.track;
      Vec3 a{x - sx * d.wheelW * 0.5f, d.wheelR, az}, b{x + sx * d.wheelW * 0.5f, d.wheelR, az};
      car.append(makeCylinder(a, b, d.wheelR, d.wheelR, 22, Vec3{1, 1, 1}, M_TIRE));
      // rim (outer face)
      Vec3 ra{x + sx * d.wheelW * 0.40f, d.wheelR, az}, rb{x + sx * d.wheelW * 0.56f, d.wheelR, az};
      car.append(makeCylinder(ra, rb, d.wheelR * 0.64f, d.wheelR * 0.64f, 20, Vec3{1, 1, 1}, M_CHROME));
      for (int s = 0; s < 5; ++s) {
        float ang = s * 2 * kPi / 5;
        Vec3 dir{0, std::cos(ang), std::sin(ang)};
        Vec3 pa{x + sx * d.wheelW * 0.57f, d.wheelR, az}, pb = pa + dir * (d.wheelR * 0.58f);
        car.append(makeCylinder(pa, pb, 0.022f, 0.016f, 6, Vec3{0.2f, 0.2f, 0.22f}, M_PLASTIC));
      }
      car.append(makeCylinder({x + sx * d.wheelW * 0.57f, d.wheelR, az}, {x + sx * d.wheelW * 0.60f, d.wheelR, az}, 0.05f, 0.05f, 10, Vec3{1, 1, 1}, M_CHROME));
    }

  // --- lights, grille, plates, mirrors ---
  float hw = d.W * 0.5f;
  float fz = -halfL, rz = halfL;
  float frontTop = curve(d.hoodTop, fz + 0.02f);
  float lampY = frontTop - 0.09f;
  for (float sx : {-1.0f, 1.0f}) {
    Mesh hl = makeRoundedBox({sx * (hw - 0.32f), lampY - 0.02f, fz + 0.09f}, {0.40f, 0.13f, 0.18f}, 0.04f, 4, Vec3{1, 1, 1}, M_LIGHT_F);
    car.append(hl);
    float rearTop = curve(d.hoodTop, rz - 0.02f);
    Mesh tl = makeRoundedBox({sx * (hw - 0.22f), d.pickup ? 0.95f : rearTop - 0.15f, rz - 0.05f}, {0.30f, d.pickup ? 0.20f : 0.17f, 0.10f}, 0.03f, 3, Vec3{1, 1, 1}, M_LIGHT_R);
    car.append(tl);
    // mirrors
    Mesh mir = makeRoundedBox({sx * (d.cabHwB + 0.13f), d.cabY0 + 0.14f, d.cabZ0 + 0.40f}, {0.20f, 0.12f, 0.10f}, 0.03f, 3, paint, M_PAINT);
    car.append(mir);
  }
  car.append(makeRoundedBox({0, frontTop - 0.2f, fz + 0.04f}, {0.78f, 0.17f, 0.10f}, 0.03f, 3, Vec3{1, 1, 1}, M_PLASTIC));  // grille
  car.append(makeRoundedBox({0, 0.40f, fz + 0.03f}, {0.46f, 0.12f, 0.03f}, 0.01f, 2, Vec3{1, 1, 1}, M_PLATE));                // front plate
  car.append(makeRoundedBox({0, d.pickup ? 0.74f : 0.62f, rz + 0.01f}, {0.46f, 0.13f, 0.03f}, 0.01f, 2, Vec3{1, 1, 1}, M_PLATE));
  // bumpers (dark plastic)
  car.append(makeRoundedBox({0, 0.34f, fz + 0.06f}, {d.W - 0.22f, 0.20f, 0.14f}, 0.05f, 4, Vec3{1, 1, 1}, M_PLASTIC));
  car.append(makeRoundedBox({0, 0.36f, rz - 0.06f}, {d.W - 0.22f, 0.20f, 0.14f}, 0.05f, 4, Vec3{1, 1, 1}, M_PLASTIC));
  return car;
}

}  // namespace bake
