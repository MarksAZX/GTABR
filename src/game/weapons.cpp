#include "weapons.h"

#include <cmath>
#include <cstring>

#include "../core/util.h"

namespace gtabr {

namespace {
// atlas cells (4 x 2 grid)
enum Mat { kSteel = 0, kGunmetal, kPolymer, kWood, kRubber, kChrome, kRedPaint, kLeather };

struct Builder {
  std::vector<gfx::ModelVertex> v;
  std::vector<uint32_t> idx;
  Mat4 xf;  // current transform
  void vert(Vec3 p, Vec3 n, Vec3 t, float u, float vv, Mat m) {
    p = xf.transformPoint(p);
    Vec3 n2{xf.m[0] * n.x + xf.m[4] * n.y + xf.m[8] * n.z, xf.m[1] * n.x + xf.m[5] * n.y + xf.m[9] * n.z,
            xf.m[2] * n.x + xf.m[6] * n.y + xf.m[10] * n.z};
    Vec3 t2{xf.m[0] * t.x + xf.m[4] * t.y + xf.m[8] * t.z, xf.m[1] * t.x + xf.m[5] * t.y + xf.m[9] * t.z,
            xf.m[2] * t.x + xf.m[6] * t.y + xf.m[10] * t.z};
    n2 = n2.normalized(); t2 = t2.normalized();
    gfx::ModelVertex o{};
    o.p[0] = p.x; o.p[1] = p.y; o.p[2] = p.z;
    auto sn = [](float x) { return (int8_t)std::lround(clamp(x, -1.0f, 1.0f) * 127.0f); };
    o.n[0] = sn(n2.x); o.n[1] = sn(n2.y); o.n[2] = sn(n2.z);
    o.t[0] = sn(t2.x); o.t[1] = sn(t2.y); o.t[2] = sn(t2.z); o.t[3] = 127;
    // cell UV with a small inset
    int cx = (int)m % 4, cy = (int)m / 4;
    o.uv[0] = (cx + 0.06f + 0.88f * (u - std::floor(u))) / 4.0f;
    o.uv[1] = (cy + 0.06f + 0.88f * (vv - std::floor(vv))) / 2.0f;
    o.w[0] = 255;
    v.push_back(o);
  }
  // box with per-face normals (centre c, half extents h)
  void box(Vec3 c, Vec3 h, Mat m) {
    const Vec3 N[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
      Vec3 n = N[f];
      Vec3 a = std::fabs(n.y) > 0.5f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
      Vec3 b = n.cross(a);
      uint32_t base = (uint32_t)v.size();
      for (int k = 0; k < 4; ++k) {
        float su = (k == 1 || k == 2) ? 1.0f : -1.0f, sv = (k >= 2) ? 1.0f : -1.0f;
        Vec3 p = c + Vec3{n.x * h.x, n.y * h.y, n.z * h.z} + Vec3{a.x * h.x, a.y * h.y, a.z * h.z} * su + Vec3{b.x * h.x, b.y * h.y, b.z * h.z} * sv;
        vert(p, n, a, su * 0.5f + 0.5f, sv * 0.5f + 0.5f, m);
      }
      idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
  }
  // lathe along Z: profile of (z, radius) pairs, optional end caps
  void lathe(const std::vector<std::pair<float, float>>& prof, int seg, Mat m, float cx = 0, float cy = 0, bool caps = true) {
    uint32_t base = (uint32_t)v.size();
    int np = (int)prof.size();
    for (int i = 0; i <= seg; ++i) {
      float a = (float)i / seg * kTau, c = std::cos(a), s = std::sin(a);
      for (int k = 0; k < np; ++k) {
        float dz = k + 1 < np ? prof[k + 1].first - prof[k].first : prof[k].first - prof[k - 1].first;
        float dr = k + 1 < np ? prof[k + 1].second - prof[k].second : prof[k].second - prof[k - 1].second;
        // slope-aware normal
        float len = std::sqrt(dz * dz + dr * dr) + 1e-6f;
        Vec3 n = Vec3{c * dz / len, s * dz / len, -dr / len};
        if (dz < 0) n = -n;
        vert({cx + prof[k].second * c, cy + prof[k].second * s, prof[k].first}, n, {-s, c, 0}, (float)i / seg, (float)k / std::max(1, np - 1), m);
      }
    }
    for (int i = 0; i < seg; ++i)
      for (int k = 0; k + 1 < np; ++k) {
        uint32_t a0 = base + i * np + k, a1 = a0 + 1, b0 = base + (i + 1) * np + k, b1 = b0 + 1;
        idx.insert(idx.end(), {a0, a1, b0, a1, b1, b0});
      }
    if (caps)
      for (int e = 0; e < 2; ++e) {
        const auto& pr = e ? prof.back() : prof.front();
        if (pr.second < 1e-4f) continue;
        float nz = (e ? 1.0f : -1.0f) * (prof.back().first > prof.front().first ? 1.0f : -1.0f);
        uint32_t cIdx = (uint32_t)v.size();
        vert({cx, cy, pr.first}, {0, 0, nz}, {1, 0, 0}, 0.5f, 0.5f, m);
        for (int i = 0; i <= seg; ++i) {
          float a = (float)i / seg * kTau;
          vert({cx + pr.second * std::cos(a), cy + pr.second * std::sin(a), pr.first}, {0, 0, nz}, {1, 0, 0}, 0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a), m);
        }
        for (int i = 0; i < seg; ++i) {
          if (nz > 0) idx.insert(idx.end(), {cIdx, cIdx + 1 + i, cIdx + 2 + i});
          else idx.insert(idx.end(), {cIdx, cIdx + 2 + i, cIdx + 1 + i});
        }
      }
  }
  void cyl(float z0, float z1, float r0, float r1, int seg, Mat m, float cx = 0, float cy = 0) { lathe({{z0, r0}, {z1, r1}}, seg, m, cx, cy); }
  void push(const Mat4& t) { xf = t; }
};

Mat4 rotX(float a) { Mat4 m; float c = std::cos(a), s = std::sin(a); m.at(1, 1) = c; m.at(1, 2) = -s; m.at(2, 1) = s; m.at(2, 2) = c; return m; }

// ---------------------------------------------------------------------------------------------------- models
void knife(Builder& b) {
  b.box({0, 0, 0.0f}, {0.014f, 0.017f, 0.06f}, kPolymer);                 // handle
  b.box({0, 0.002f, -0.065f}, {0.02f, 0.022f, 0.006f}, kSteel);           // bolster
  // blade: thin, tapering box segments
  for (int i = 0; i < 5; ++i) {
    float z = -0.08f - i * 0.03f, h = 0.022f - i * 0.0035f;
    b.box({0, 0.004f + i * 0.0015f, z}, {0.0025f, h, 0.016f}, kChrome);
  }
}
void baton(Builder& b) {
  b.cyl(0.06f, -0.12f, 0.017f, 0.017f, 12, kRubber);                     // grip
  b.cyl(-0.12f, -0.56f, 0.016f, 0.018f, 12, kPolymer);                   // shaft
  b.lathe({{-0.56f, 0.018f}, {-0.575f, 0.016f}, {-0.58f, 0.0f}}, 12, kPolymer, 0, 0, false);
  // side handle
  Mat4 t = Mat4::translation({0, 0.0f, -0.09f}) * rotX(kPi * 0.5f);
  b.push(t);
  b.cyl(0.0f, -0.12f, 0.014f, 0.014f, 10, kRubber);
  b.push(Mat4());
}
void crowbar(Builder& b) {
  b.cyl(0.08f, -0.5f, 0.012f, 0.012f, 8, kRedPaint);
  // hooked end (bent back and up)
  for (int i = 0; i < 6; ++i) {
    float a0 = i / 6.0f * 2.6f, a1 = (i + 1) / 6.0f * 2.6f;
    Vec3 p0{0, 0.045f - 0.045f * std::cos(a0), 0.08f + 0.045f * std::sin(a0)};
    Vec3 p1{0, 0.045f - 0.045f * std::cos(a1), 0.08f + 0.045f * std::sin(a1)};
    Vec3 d = p1 - p0;
    float L = d.length();
    Mat4 t = Mat4::translation(p0) * rotX(-std::atan2(d.y, d.z));
    b.push(t);
    b.cyl(0.0f, L, 0.012f, 0.012f, 8, kRedPaint);
  }
  b.push(Mat4::translation({0, 0, -0.5f}));
  b.lathe({{0.0f, 0.012f}, {-0.03f, 0.01f}, {-0.05f, 0.004f}}, 8, kSteel);    // flat chisel end
  b.push(Mat4());
}
void bat(Builder& b) {
  b.lathe({{0.08f, 0.024f}, {0.075f, 0.016f}, {-0.05f, 0.014f}, {-0.25f, 0.02f}, {-0.45f, 0.031f}, {-0.66f, 0.034f}, {-0.70f, 0.03f}, {-0.71f, 0.0f}},
          16, kWood);
  b.cyl(0.07f, -0.11f, 0.0165f, 0.0165f, 14, kLeather);   // tape
}
void pistol(Builder& b) {
  b.box({0, 0.065f, -0.05f}, {0.014f, 0.017f, 0.095f}, kGunmetal);     // slide
  b.box({0, 0.04f, -0.035f}, {0.013f, 0.012f, 0.08f}, kPolymer);       // frame
  Mat4 g = Mat4::translation({0, 0.0f, 0.01f}) * rotX(-0.28f);
  b.push(g);
  b.box({0, -0.02f, 0}, {0.013f, 0.055f, 0.022f}, kPolymer);           // grip
  b.push(Mat4());
  b.box({0, 0.018f, -0.025f}, {0.004f, 0.012f, 0.018f}, kPolymer);     // trigger guard
  b.cyl(-0.145f, -0.15f, 0.005f, 0.005f, 8, kSteel, 0, 0.065f);        // muzzle
}
void revolver(Builder& b) {
  b.cyl(-0.03f, -0.2f, 0.009f, 0.009f, 12, kChrome, 0, 0.06f);         // barrel
  b.box({0, 0.07f, -0.11f}, {0.004f, 0.005f, 0.09f}, kChrome);         // rib
  b.cyl(0.02f, -0.03f, 0.021f, 0.021f, 12, kChrome, 0, 0.048f);        // cylinder
  b.box({0, 0.045f, 0.0f}, {0.011f, 0.025f, 0.035f}, kChrome);         // frame
  Mat4 g = Mat4::translation({0, 0.01f, 0.03f}) * rotX(-0.35f);
  b.push(g);
  b.box({0, -0.025f, 0}, {0.013f, 0.05f, 0.02f}, kWood);               // wood grip
  b.push(Mat4());
}
void smg(Builder& b) {
  b.box({0, 0.05f, -0.06f}, {0.02f, 0.03f, 0.13f}, kGunmetal);          // receiver
  b.cyl(-0.19f, -0.27f, 0.011f, 0.011f, 10, kSteel, 0, 0.055f);        // barrel
  b.box({0, -0.005f, -0.02f}, {0.012f, 0.05f, 0.016f}, kPolymer);       // grip
  b.box({0, -0.03f, -0.1f}, {0.01f, 0.07f, 0.014f}, kSteel);            // magazine
  b.box({0, 0.04f, 0.12f}, {0.008f, 0.012f, 0.06f}, kSteel);            // folded stock
  b.box({0, 0.087f, -0.06f}, {0.004f, 0.006f, 0.02f}, kGunmetal);       // sight
}
void shotgun(Builder& b) {
  b.cyl(-0.05f, -0.62f, 0.013f, 0.013f, 12, kGunmetal, 0, 0.055f);      // barrel
  b.cyl(-0.12f, -0.5f, 0.011f, 0.011f, 10, kGunmetal, 0, 0.032f);       // magazine tube
  b.box({0, 0.03f, -0.28f}, {0.018f, 0.016f, 0.07f}, kWood);            // pump
  b.box({0, 0.045f, 0.0f}, {0.017f, 0.025f, 0.07f}, kGunmetal);         // receiver
  Mat4 st = Mat4::translation({0, 0.02f, 0.07f}) * rotX(0.18f);
  b.push(st);
  b.box({0, 0, 0.14f}, {0.018f, 0.035f, 0.14f}, kWood);                 // stock
  b.push(Mat4());
  b.box({0, -0.005f, 0.035f}, {0.012f, 0.03f, 0.014f}, kWood);          // wrist
}

void atlas(std::vector<uint8_t>& alb, std::vector<uint8_t>& orm, uint32_t W, uint32_t H) {
  alb.assign((size_t)W * H * 4, 255);
  orm.assign((size_t)W * H * 4, 255);
  uint32_t seed = 99;
  auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) & 0xFFFF) / 65535.0f; };
  struct M { float r, g, b, rough, metal, grain; };
  const M mats[8] = {
      {0.56f, 0.57f, 0.58f, 0.32f, 1.0f, 0.04f},   // steel
      {0.10f, 0.10f, 0.11f, 0.42f, 1.0f, 0.03f},   // gunmetal
      {0.035f, 0.035f, 0.04f, 0.6f, 0.0f, 0.02f},  // polymer
      {0.42f, 0.24f, 0.11f, 0.55f, 0.0f, 0.18f},   // wood
      {0.03f, 0.03f, 0.03f, 0.88f, 0.0f, 0.04f},   // rubber
      {0.78f, 0.79f, 0.8f, 0.14f, 1.0f, 0.02f},    // chrome
      {0.55f, 0.05f, 0.04f, 0.45f, 0.0f, 0.06f},   // red paint (worn)
      {0.12f, 0.07f, 0.04f, 0.7f, 0.0f, 0.08f},    // leather tape
  };
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      int cell = (int)(x * 4 / W) + 4 * (int)(y * 2 / H);
      const M& m = mats[cell];
      float u = (float)(x % (W / 4)) / (W / 4), v = (float)(y % (H / 2)) / (H / 2);
      float n = rnd() * 2.0f - 1.0f;
      float grain = m.grain * n;
      if (cell == kWood) grain += 0.12f * std::sin(v * 60.0f + std::sin(u * 9.0f) * 3.0f);
      float wear = 0;
      if (cell == kRedPaint && rnd() > 0.93f) wear = 1.0f;   // chipped paint shows steel
      float r = m.r * (1 + grain), g = m.g * (1 + grain), b = m.b * (1 + grain);
      float rough = m.rough + n * 0.05f, metal = m.metal;
      if (wear > 0) { r = 0.5f; g = 0.5f; b = 0.5f; metal = 1.0f; rough = 0.4f; }
      auto srgb = [](float l) { return (uint8_t)std::lround(clamp(std::pow(clamp(l, 0.0f, 1.0f), 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f); };
      size_t i = ((size_t)y * W + x) * 4;
      alb[i] = srgb(r); alb[i + 1] = srgb(g); alb[i + 2] = srgb(b); alb[i + 3] = 255;
      orm[i] = 255; orm[i + 1] = (uint8_t)(clamp(rough, 0.05f, 1.0f) * 255); orm[i + 2] = (uint8_t)(metal * 255); orm[i + 3] = 255;
    }
}
}  // namespace

bool buildWeaponMeshes(gfx::Renderer& r, WeaponMeshes& out) {
  void (*fns[kWeaponCount])(Builder&) = {nullptr, knife, baton, crowbar, bat, pistol, revolver, smg, shotgun};
  const Vec3 muzzles[kWeaponCount] = {{}, {}, {}, {}, {}, {0, 0.065f, -0.15f}, {0, 0.06f, -0.2f}, {0, 0.055f, -0.27f}, {0, 0.055f, -0.62f}};
  for (int w = 1; w < kWeaponCount; ++w) {
    Builder b;
    fns[w](b);
    gfx::ModelLod lod{0, (uint32_t)b.idx.size()};
    out.mesh[w] = r.createModel(b.v.data(), b.v.size(), b.idx.data(), b.idx.size(), &lod, 1, false);
    out.muzzle[w] = muzzles[w];
  }
  std::vector<uint8_t> alb, orm;
  const uint32_t W = 512, H = 256;
  atlas(alb, orm, W, H);
  gfx::TexHandle ta = r.createTextureRGBA(W, H, alb.data(), true, true, gfx::SamplerKind::ClampLinear);
  gfx::TexHandle tm = r.createTextureRGBA(W, H, orm.data(), false, true, gfx::SamplerKind::ClampLinear);
  out.material = r.createModelMaterial(ta, {}, tm);
  out.ok = true;
  return true;
}

}  // namespace gtabr
