// Trees, shrubs and street props baked as sprites.
#include <cmath>

#include "models.h"
#include "core/util.h"

namespace bake {
namespace {
using gtabr::Rng;

// A round leaf clump made of a fan of triangles facing 'n'.
void addLeaf(Mesh& m, Vec3 c, Vec3 n, float r, Vec3 col, Rng& rng, uint16_t mat) {
  Vec3 ref = std::fabs(n.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  Vec3 u = n.cross(ref).normalized(), w = n.cross(u).normalized();
  uint32_t base = (uint32_t)m.v.size();
  m.v.push_back({c, n, col});
  const int N = 7;
  for (int i = 0; i < N; ++i) {
    float a = (float)i / N * 2 * kPi + rng.uni() * 0.4f;
    float rr = r * (0.75f + rng.uni() * 0.5f);
    m.v.push_back({c + (u * std::cos(a) + w * std::sin(a)) * rr, n, col * 0.92f});
  }
  for (int i = 0; i < N; ++i) m.t.push_back({base, base + 1 + i, base + 1 + (i + 1) % N, mat});
}
}  // namespace

Mesh buildTree(int type, unsigned seed) {
  Mesh m;
  Rng rng(seed * 7919u + 17u);
  if (type == 0) {  // broadleaf shade tree ~7m
    float h = 3.0f;
    // trunk with slight lean + roots flare
    m.append(makeCylinder({0, 0, 0}, {0.08f, h * 0.55f, 0.04f}, 0.30f, 0.17f, 10, {0.33f, 0.25f, 0.18f}, M_BARK));
    m.append(makeCylinder({0.08f, h * 0.55f, 0.04f}, {0.0f, h * 1.05f, 0.1f}, 0.17f, 0.11f, 10, {0.31f, 0.23f, 0.16f}, M_BARK));
    for (int b = 0; b < 4; ++b) {
      float a = b * kPi / 2 + rng.uni();
      m.append(makeCylinder({0.0f, h * 0.95f, 0.1f}, {std::cos(a) * 1.5f, h * 1.55f + rng.uni() * 0.4f, 0.1f + std::sin(a) * 1.5f}, 0.1f, 0.05f, 8, {0.3f, 0.22f, 0.15f}, M_BARK));
    }
    Vec3 cc{0.0f, h * 1.9f, 0.1f};
    Vec3 rad{2.9f, 2.2f, 2.8f};
    for (int i = 0; i < 1500; ++i) {
      // sample points in the ellipsoid biased towards the shell
      Vec3 d;
      do { d = {rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1)}; } while (d.lengthSq() > 1.0f || d.lengthSq() < 0.05f);
      float r = std::pow(d.length(), 0.35f);
      Vec3 dir = d.normalized();
      Vec3 p = cc + Vec3{dir.x * rad.x, dir.y * rad.y, dir.z * rad.z} * r;
      if (p.y < h * 1.1f) p.y = h * 1.1f + rng.uni() * 0.3f;
      Vec3 n = (dir * 0.85f + Vec3{rng.range(-0.4f, 0.4f), rng.range(-0.3f, 0.5f), rng.range(-0.4f, 0.4f)}).normalized();
      float shade = 0.55f + 0.45f * r;               // inner leaves are darker
      float hue = rng.uni();
      Vec3 col = Vec3{0.13f + 0.06f * hue, 0.30f + 0.10f * hue, 0.07f + 0.03f * hue} * (0.7f + 0.5f * shade * (0.6f + 0.4f * (dir.y * 0.5f + 0.5f)));
      addLeaf(m, p, n, 0.30f + rng.uni() * 0.22f, col, rng, M_FOLIAGE);
    }
  } else if (type == 1) {  // palm ~8m
    const int segs = 18;
    Vec3 prev{0, 0, 0};
    float lean = 0.5f;
    for (int i = 1; i <= segs; ++i) {
      float t = (float)i / segs;
      Vec3 p{lean * t * t, t * 7.2f, 0.15f * std::sin(t * 3.0f)};
      m.append(makeCylinder(prev, p, 0.2f - 0.07f * (t - 1.0f / segs), 0.2f - 0.07f * t, 10, i % 2 ? Vec3{0.45f, 0.38f, 0.30f} : Vec3{0.38f, 0.31f, 0.25f}, M_BARK));
      prev = p;
    }
    Vec3 top = prev;
    for (int f = 0; f < 13; ++f) {
      float a = f * 2 * kPi / 13 + rng.uni() * 0.3f;
      float len = 3.0f + rng.uni() * 0.9f, droop = 1.2f + rng.uni() * 0.6f;
      Vec3 dir{std::cos(a), 0, std::sin(a)};
      uint32_t prevL = 0, prevR = 0;
      const int FS = 12;
      for (int i = 0; i <= FS; ++i) {
        float t = (float)i / FS;
        Vec3 c = top + dir * (len * t) + Vec3{0, 0.9f * std::sin(t * 2.6f) - droop * t * t, 0};
        float wdt = 0.55f * std::sin(t * kPi * 0.9f + 0.15f) * (i % 2 ? 1.0f : 0.72f);
        Vec3 side = Vec3{-dir.z, 0, dir.x};
        Vec3 nrm = (Vec3{0, 1, 0} + dir * 0.2f).normalized();
        Vec3 col = Vec3{0.12f, 0.33f, 0.09f} * (0.75f + 0.4f * (1 - t)) ;
        uint32_t base = (uint32_t)m.v.size();
        m.v.push_back({c + side * wdt + Vec3{0, -0.18f * wdt, 0}, nrm, col});
        m.v.push_back({c - side * wdt + Vec3{0, -0.18f * wdt, 0}, nrm, col});
        if (i > 0) {
          m.t.push_back({prevL, prevR, base, M_FOLIAGE});
          m.t.push_back({prevR, base + 1, base, M_FOLIAGE});
        }
        prevL = base; prevR = base + 1;
      }
    }
    // coconuts
    for (int i = 0; i < 4; ++i) m.append(makeEllipsoid(top + Vec3{rng.range(-0.2f, 0.2f), -0.25f, rng.range(-0.2f, 0.2f)}, {0.12f, 0.14f, 0.12f}, 8, 6, {0.25f, 0.2f, 0.1f}, M_BARK));
  } else {  // shrub
    for (int i = 0; i < 260; ++i) {
      Vec3 d;
      do { d = {rng.range(-1, 1), rng.range(0, 1), rng.range(-1, 1)}; } while (d.lengthSq() > 1.0f || d.lengthSq() < 0.03f);
      Vec3 dir = d.normalized();
      float r = std::pow(d.length(), 0.4f);
      Vec3 p = Vec3{dir.x * 0.85f, 0.15f + dir.y * 0.75f, dir.z * 0.85f} * r + Vec3{0, 0.1f, 0};
      Vec3 n = (dir * 0.9f + Vec3{rng.range(-0.3f, 0.3f), 0.2f, rng.range(-0.3f, 0.3f)}).normalized();
      float hue = rng.uni();
      Vec3 col = Vec3{0.1f + 0.08f * hue, 0.28f + 0.1f * hue, 0.07f} * (0.6f + 0.6f * r);
      addLeaf(m, p, n, 0.16f + rng.uni() * 0.1f, col, rng, M_FOLIAGE);
    }
  }
  return m;
}

Mesh buildProp(const std::string& name) {
  Mesh m;
  if (name == "lixeira") {
    m.append(makeCylinder({0, 0.03f, 0}, {0, 0.95f, 0}, 0.30f, 0.37f, 16, {0.16f, 0.45f, 0.22f}, M_MATTE));
    m.append(makeCylinder({0, 0.95f, 0}, {0, 1.0f, 0}, 0.40f, 0.40f, 16, {0.13f, 0.38f, 0.19f}, M_MATTE));
    m.append(makeCylinder({0, 0.42f, -0.31f}, {0, 0.48f, -0.31f}, 0.1f, 0.1f, 8, {0.1f, 0.1f, 0.1f}, M_PLASTIC));
    m.append(makeCylinder({-0.25f, 0.03f, 0.25f}, {-0.25f, 0.08f, 0.25f}, 0.05f, 0.05f, 6, {0.05f, 0.05f, 0.05f}, M_RUBBER));
  } else if (name == "cone") {
    m.append(makeRoundedBox({0, 0.02f, 0}, {0.42f, 0.04f, 0.42f}, 0.01f, 2, {0.1f, 0.1f, 0.1f}, M_RUBBER));
    m.append(makeCylinder({0, 0.04f, 0}, {0, 0.72f, 0}, 0.17f, 0.03f, 14, {0.95f, 0.38f, 0.08f}, M_MATTE));
    m.append(makeCylinder({0, 0.30f, 0}, {0, 0.38f, 0}, 0.108f, 0.1f, 14, {0.95f, 0.95f, 0.92f}, M_MATTE, false));
  } else if (name == "caixas") {
    m.append(makeRoundedBox({0, 0.22f, 0}, {0.7f, 0.44f, 0.5f}, 0.02f, 3, {0.62f, 0.46f, 0.28f}, M_MATTE));
    m.append(makeRoundedBox({0.05f, 0.62f, 0.02f}, {0.5f, 0.36f, 0.4f}, 0.02f, 3, {0.66f, 0.5f, 0.31f}, M_MATTE));
    m.append(makeRoundedBox({-0.55f, 0.15f, 0.1f}, {0.36f, 0.3f, 0.36f}, 0.02f, 3, {0.58f, 0.43f, 0.26f}, M_MATTE));
  } else if (name == "banco") {
    for (int i = 0; i < 5; ++i) m.append(makeRoundedBox({0, 0.45f, -0.18f + i * 0.09f}, {1.5f, 0.035f, 0.07f}, 0.01f, 2, {0.45f, 0.30f, 0.17f}, M_MATTE));
    for (int i = 0; i < 3; ++i) m.append(makeRoundedBox({0, 0.78f - i * 0.1f, 0.22f - i * 0.0f}, {1.5f, 0.07f, 0.03f}, 0.01f, 2, {0.45f, 0.30f, 0.17f}, M_MATTE));
    for (float sx : {-0.65f, 0.65f}) {
      m.append(makeRoundedBox({sx, 0.22f, 0}, {0.06f, 0.45f, 0.5f}, 0.015f, 2, {0.12f, 0.12f, 0.13f}, M_METAL));
      m.append(makeRoundedBox({sx, 0.6f, 0.23f}, {0.06f, 0.4f, 0.04f}, 0.015f, 2, {0.12f, 0.12f, 0.13f}, M_METAL));
    }
  } else if (name == "orelhao") {
    m.append(makeCylinder({0, 0, 0}, {0, 1.5f, 0}, 0.05f, 0.05f, 8, {0.3f, 0.3f, 0.32f}, M_METAL));
    // fibreglass shell: half ellipsoid dome (open to the front)
    Mesh dome = makeEllipsoid({0, 1.62f, 0}, {0.42f, 0.42f, 0.34f}, 20, 14, {0.95f, 0.55f, 0.12f}, M_PAINT);
    m.append(dome);
    m.append(makeRoundedBox({0, 1.26f, -0.12f}, {0.34f, 0.22f, 0.2f}, 0.03f, 3, {0.15f, 0.15f, 0.17f}, M_PLASTIC));
  } else if (name == "pneus") {
    for (int i = 0; i < 4; ++i) m.append(makeCylinder({0, 0.09f + i * 0.2f, 0}, {0, 0.19f + i * 0.2f, 0}, 0.32f, 0.32f, 16, {0.05f, 0.05f, 0.055f}, M_TIRE));
    for (int i = 0; i < 3; ++i) m.append(makeCylinder({0.8f, 0.09f + i * 0.2f, 0.1f}, {0.8f, 0.19f + i * 0.2f, 0.1f}, 0.32f, 0.32f, 16, {0.05f, 0.05f, 0.055f}, M_TIRE));
  } else if (name == "tambor") {
    m.append(makeCylinder({0, 0.02f, 0}, {0, 0.88f, 0}, 0.29f, 0.29f, 16, {0.1f, 0.3f, 0.65f}, M_PAINT));
    for (float y : {0.2f, 0.45f, 0.7f}) m.append(makeCylinder({0, y - 0.015f, 0}, {0, y + 0.015f, 0}, 0.3f, 0.3f, 16, {0.08f, 0.22f, 0.5f}, M_PAINT, false));
  } else if (name == "hidrante") {
    m.append(makeCylinder({0, 0, 0}, {0, 0.62f, 0}, 0.14f, 0.12f, 12, {0.8f, 0.1f, 0.08f}, M_PAINT));
    m.append(makeEllipsoid({0, 0.68f, 0}, {0.15f, 0.1f, 0.15f}, 12, 8, {0.8f, 0.1f, 0.08f}, M_PAINT));
    m.append(makeCylinder({-0.2f, 0.4f, 0}, {0.2f, 0.4f, 0}, 0.06f, 0.06f, 8, {0.75f, 0.1f, 0.08f}, M_PAINT));
  } else if (name == "vaso") {
    m.append(makeCylinder({0, 0.02f, 0}, {0, 0.5f, 0}, 0.2f, 0.28f, 14, {0.72f, 0.35f, 0.2f}, M_MATTE));
    Mesh shrub = buildTree(2, 3);
    transformMesh(shrub, Xf::scale({0.42f, 0.5f, 0.42f}));
    transformMesh(shrub, Xf::translate({0, 0.42f, 0}));
    m.append(shrub);
  } else if (name == "bomba") {
    // fuel pump (fictional blue/yellow livery, matches the station)
    Vec3 blue{0.07f, 0.22f, 0.62f}, yellow{0.98f, 0.78f, 0.1f};
    m.append(makeRoundedBox({0, 0.08f, 0}, {0.62f, 0.16f, 0.5f}, 0.03f, 3, {0.55f, 0.55f, 0.57f}, M_MATTE));
    m.append(makeRoundedBox({0, 0.85f, 0}, {0.50f, 1.5f, 0.36f}, 0.05f, 5, blue, M_PAINT));
    m.append(makeRoundedBox({0, 1.52f, 0}, {0.56f, 0.2f, 0.42f}, 0.04f, 4, yellow, M_PAINT));
    for (float sz : {-1.0f, 1.0f}) {
      m.append(makeRoundedBox({0, 1.2f, sz * 0.185f}, {0.34f, 0.2f, 0.02f}, 0.01f, 2, {0.04f, 0.1f, 0.06f}, M_GLASS));
      m.append(makeRoundedBox({0, 1.2f, sz * 0.197f}, {0.26f, 0.09f, 0.01f}, 0.004f, 2, {0.2f, 0.95f, 0.35f}, M_LIGHT_F));
      m.append(makeRoundedBox({0, 0.95f, sz * 0.19f}, {0.34f, 0.1f, 0.02f}, 0.01f, 2, yellow, M_PAINT));
      m.append(makeRoundedBox({0.2f, 0.78f, sz * 0.2f}, {0.09f, 0.3f, 0.05f}, 0.02f, 3, {0.1f, 0.1f, 0.1f}, M_PLASTIC));
      m.append(makeCylinder({0.2f, 0.78f, sz * 0.24f}, {0.2f, 0.3f, sz * 0.3f}, 0.012f, 0.012f, 6, {0.05f, 0.05f, 0.05f}, M_RUBBER));
    }
  } else if (name == "poste_placa") {
    m.append(makeCylinder({0, 0, 0}, {0, 2.2f, 0}, 0.035f, 0.035f, 8, {0.4f, 0.4f, 0.42f}, M_METAL));
    m.append(makeRoundedBox({0, 2.1f, 0}, {0.6f, 0.6f, 0.03f}, 0.02f, 3, {0.9f, 0.9f, 0.9f}, M_MATTE));
    m.append(makeRoundedBox({0, 2.1f, -0.002f}, {0.5f, 0.5f, 0.031f}, 0.02f, 3, {0.1f, 0.3f, 0.75f}, M_MATTE));
  }
  return m;
}

}  // namespace bake
