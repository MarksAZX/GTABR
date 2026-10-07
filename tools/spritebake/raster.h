// Offline software renderer used to bake multi-directional sprites from procedural 3D models.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/math.h"

namespace bake {
using gtabr::Vec3;
using gtabr::kPi;

struct Material {
  Vec3 albedo{0.5f, 0.5f, 0.5f};
  float spec = 0.15f;       // specular strength
  float shin = 24.0f;       // blinn exponent
  float env = 0.0f;         // environment reflection strength
  float emissive = 0.0f;
  float aoLow = 1.0f;       // AO multiplier near the ground (1 = none)
  bool useVertexColor = true;
};

struct Vtx {
  Vec3 p, n, c;
  float aux = 0.0f;  // blend factor towards the triangle's secondary material
};
struct Tri {
  uint32_t a, b, c;
  uint16_t mat;
  uint16_t mat2 = 0xFFFF;
};

struct Mesh {
  std::vector<Vtx> v;
  std::vector<Tri> t;
  void append(const Mesh& o) {
    uint32_t base = (uint32_t)v.size();
    v.insert(v.end(), o.v.begin(), o.v.end());
    for (Tri tr : o.t) { tr.a += base; tr.b += base; tr.c += base; t.push_back(tr); }
  }
};

// Affine transform: rotation (row-major 3x3) + translation.
struct Xf {
  float r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  Vec3 t{0, 0, 0};
  Vec3 apply(const Vec3& p) const {
    return {r[0] * p.x + r[1] * p.y + r[2] * p.z + t.x, r[3] * p.x + r[4] * p.y + r[5] * p.z + t.y,
            r[6] * p.x + r[7] * p.y + r[8] * p.z + t.z};
  }
  Vec3 rot(const Vec3& p) const {
    return {r[0] * p.x + r[1] * p.y + r[2] * p.z, r[3] * p.x + r[4] * p.y + r[5] * p.z, r[6] * p.x + r[7] * p.y + r[8] * p.z};
  }
  Xf operator*(const Xf& o) const {  // this * o (apply o first)
    Xf x;
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) x.r[i * 3 + j] = r[i * 3 + 0] * o.r[0 + j] + r[i * 3 + 1] * o.r[3 + j] + r[i * 3 + 2] * o.r[6 + j];
    x.t = apply(o.t);
    return x;
  }
  static Xf translate(Vec3 t) { Xf x; x.t = t; return x; }
  static Xf rotX(float a) { Xf x; float c = std::cos(a), s = std::sin(a); float r[9] = {1, 0, 0, 0, c, -s, 0, s, c}; std::copy(r, r + 9, x.r); return x; }
  // yaw convention of the game: +a turns clockwise seen from above; (0,0,-1) -> (sin a, 0, -cos a)
  static Xf rotY(float a) { Xf x; float c = std::cos(a), s = std::sin(a); float r[9] = {c, 0, -s, 0, 1, 0, s, 0, c}; std::copy(r, r + 9, x.r); return x; }
  static Xf rotZ(float a) { Xf x; float c = std::cos(a), s = std::sin(a); float r[9] = {c, -s, 0, s, c, 0, 0, 0, 1}; std::copy(r, r + 9, x.r); return x; }
  static Xf scale(Vec3 s) { Xf x; x.r[0] = s.x; x.r[4] = s.y; x.r[8] = s.z; return x; }
};

inline void transformMesh(Mesh& m, const Xf& x) {
  for (auto& v : m.v) { v.p = x.apply(v.p); v.n = x.rot(v.n).normalized(); }
}

// ---- primitives (all produce smooth normals) ----
Mesh makeEllipsoid(Vec3 c, Vec3 radii, int seg, int rings, Vec3 col, uint16_t mat);
Mesh makeRoundedBox(Vec3 c, Vec3 size, float radius, int seg, Vec3 col, uint16_t mat);
Mesh makeCylinder(Vec3 a, Vec3 b, float ra, float rb, int seg, Vec3 col, uint16_t mat, bool caps = true);
Mesh makeCapsule(Vec3 a, Vec3 b, float ra, float rb, int seg, Vec3 col, uint16_t mat);

// Camera-space bake parameters.
struct BakeView {
  float pitchDeg;  // elevation of the camera above the horizon
  float ppm;       // pixels per metre
  int ss;          // supersampling factor per axis
};

struct Image {
  int w = 0, h = 0;
  std::vector<float> rgba;  // straight alpha
};

struct BakeResult {
  Image img;        // tight cropped
  float pivotX, pivotY;  // pivot (world origin) in pixels relative to cropped image top-left
};

struct Lighting {
  Vec3 keyDir{-0.45f, 0.75f, 0.5f};
  Vec3 keyCol{1.0f, 0.95f, 0.88f};
  float keyI = 0.95f;
  Vec3 fillDir{0.7f, 0.35f, 0.3f};
  Vec3 fillCol{0.55f, 0.68f, 1.0f};
  float fillI = 0.28f;
  Vec3 skyCol{0.50f, 0.62f, 0.85f};
  Vec3 gndCol{0.30f, 0.27f, 0.24f};
  float ambI = 0.55f;
};

BakeResult bakeMesh(const Mesh& mesh, const std::vector<Material>& mats, const BakeView& view, float yawRad, const Lighting& L,
                    int maxW = 1400, int maxH = 1400);

}  // namespace bake
