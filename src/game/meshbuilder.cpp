#include "meshbuilder.h"

#include <cmath>

namespace gtabr {

uint32_t MeshBuilder::vert(const Vec3& p0, const Vec3& n0, const Vec2& uv, int layer, float ao) {
  gfx::WorldVertex w{};
  Vec3 p = p0, n = n0;
  if (xf_) {
    p = Vec3{p0.x * cy_ + p0.z * sy_, p0.y, -p0.x * sy_ + p0.z * cy_} * sc_ + pos_;
    n = Vec3{n0.x * cy_ + n0.z * sy_, n0.y, -n0.x * sy_ + n0.z * cy_};
  }
  w.p[0] = p.x; w.p[1] = p.y; w.p[2] = p.z;
  w.n[0] = (int8_t)std::lround(n.x * 127); w.n[1] = (int8_t)std::lround(n.y * 127); w.n[2] = (int8_t)std::lround(n.z * 127);
  w.n[3] = (int8_t)std::lround(clamp(emissive_, 0.0f, 1.0f) * 127);
  w.uv[0] = uv.x; w.uv[1] = uv.y;
  w.color = packRGBA8(tint_.x, tint_.y, tint_.z, ao);
  w.layer = (float)layer + 0.4f * clamp(mud_, 0.0f, 1.0f);   // fractional part (< 0.5) = mud amount; whole part = material layer
  m_->v.push_back(w);
  m_->bounds.expand(p);
  return (uint32_t)m_->v.size() - 1;
}

void MeshBuilder::quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec2& ua, const Vec2& ub, const Vec2& uc,
                       const Vec2& ud, int layer, float aoA, float aoB, float aoC, float aoD) {
  Vec3 n = (b - a).cross(d - a).normalized();
  uint32_t i0 = vert(a, n, ua, layer, aoA), i1 = vert(b, n, ub, layer, aoB), i2 = vert(c, n, uc, layer, aoC), i3 = vert(d, n, ud, layer, aoD);
  m_->idx.insert(m_->idx.end(), {i0, i1, i2, i0, i2, i3});
}

void MeshBuilder::groundRect(float x0, float z0, float x1, float z1, float y, int layer, float tile, float uOff, float vOff) {
  // seen from above: counter-clockwise = (x0,z1) -> (x1,z1) -> (x1,z0) -> (x0,z0)
  quad({x0, y, z1}, {x1, y, z1}, {x1, y, z0}, {x0, y, z0}, {x0 / tile + uOff, z1 / tile + vOff}, {x1 / tile + uOff, z1 / tile + vOff},
       {x1 / tile + uOff, z0 / tile + vOff}, {x0 / tile + uOff, z0 / tile + vOff}, layer);
}

void MeshBuilder::groundRectMud(float x0, float z0, float x1, float z1, float y, int layer, float tile, float mNW, float mNE, float mSE, float mSW) {
  // corners in the same order as groundRect: (x0,z1) (x1,z1) (x1,z0) (x0,z0); "N" is the z0 side
  Vec3 n{0, 1, 0};
  mud_ = mSW; uint32_t i0 = vert({x0, y, z1}, n, {x0 / tile, z1 / tile}, layer, 1.0f);
  mud_ = mSE; uint32_t i1 = vert({x1, y, z1}, n, {x1 / tile, z1 / tile}, layer, 1.0f);
  mud_ = mNE; uint32_t i2 = vert({x1, y, z0}, n, {x1 / tile, z0 / tile}, layer, 1.0f);
  mud_ = mNW; uint32_t i3 = vert({x0, y, z0}, n, {x0 / tile, z0 / tile}, layer, 1.0f);
  mud_ = 0;
  m_->idx.insert(m_->idx.end(), {i0, i1, i2, i0, i2, i3});
}

void MeshBuilder::roofRect(float x0, float z0, float x1, float z1, float y, int layer, float tile) { groundRect(x0, z0, x1, z1, y, layer, tile); }

void MeshBuilder::ceiling(float x0, float z0, float x1, float z1, float y, int layer, float tile) {
  quad({x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}, {x0 / tile, z0 / tile}, {x1 / tile, z0 / tile}, {x1 / tile, z1 / tile}, {x0 / tile, z1 / tile}, layer);
}

void MeshBuilder::wall(float x0, float z0, float x1, float z1, float y0, float y1, int layer, float u0, float u1, float v0, float v1,
                       float aoBottom, float aoTop) {
  // v0 = texture v at the top edge, v1 = texture v at the bottom edge
  quad({x0, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z0}, {u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}, layer, aoBottom, aoBottom, aoTop, aoTop);
}

void MeshBuilder::box(const AABB& b, int layerSide, int layerTop, float tile, bool bottom) {
  const Vec3 &mn = b.mn, &mx = b.mx;
  float h = mx.y - mn.y;
  // top
  groundRect(mn.x, mn.z, mx.x, mx.z, mx.y, layerTop, tile);
  // sides: world-space UVs so textures keep a constant scale
  auto side = [&](float x0, float z0, float x1, float z1) {
    float len = std::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
    wall(x0, z0, x1, z1, mn.y, mx.y, layerSide, 0, len / tile, 0, h / tile, 0.8f, 1.0f);
  };
  side(mn.x, mx.z, mx.x, mx.z);  // south (+z) faces outward +z: left->right is +x
  side(mx.x, mx.z, mx.x, mn.z);  // east (+x)
  side(mx.x, mn.z, mn.x, mn.z);  // north (-z)
  side(mn.x, mn.z, mn.x, mx.z);  // west (-x)
  if (bottom) quad({mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}, {0, 0}, {1, 0}, {1, 1}, {0, 1}, layerSide);
}

void MeshBuilder::prism(Vec3 base, float r, float height, int sides, int layer, float tile) {
  for (int i = 0; i < sides; ++i) {
    float a0 = (float)i / sides * kTau, a1 = (float)(i + 1) / sides * kTau;
    Vec3 p0{base.x + std::cos(a0) * r, base.y, base.z + std::sin(a0) * r}, p1{base.x + std::cos(a1) * r, base.y, base.z + std::sin(a1) * r};
    Vec3 n0 = Vec3{std::cos(a0), 0, std::sin(a0)}, n1 = Vec3{std::cos(a1), 0, std::sin(a1)};
    Vec3 q0 = p0 + Vec3{0, height, 0}, q1 = p1 + Vec3{0, height, 0};
    // outward when seen from outside: p1 -> p0 order gives CCW for outward normal
    uint32_t i0 = vert(p1, n1, {1, 1}, layer, 0.85f), i1 = vert(p0, n0, {0, 1}, layer, 0.85f), i2 = vert(q0, n0, {0, 0}, layer, 1.0f),
             i3 = vert(q1, n1, {1, 0}, layer, 1.0f);
    m_->idx.insert(m_->idx.end(), {i0, i1, i2, i0, i2, i3});
  }
  (void)tile;
  // cap
  uint32_t c = vert(base + Vec3{0, height, 0}, {0, 1, 0}, {0.5f, 0.5f}, layer, 1.0f);
  for (int i = 0; i < sides; ++i) {
    float a0 = (float)i / sides * kTau, a1 = (float)(i + 1) / sides * kTau;
    uint32_t i0 = vert({base.x + std::cos(a0) * r, base.y + height, base.z + std::sin(a0) * r}, {0, 1, 0}, {0, 0}, layer, 1.0f);
    uint32_t i1 = vert({base.x + std::cos(a1) * r, base.y + height, base.z + std::sin(a1) * r}, {0, 1, 0}, {1, 0}, layer, 1.0f);
    m_->idx.insert(m_->idx.end(), {c, i1, i0});
  }
}

void MeshBuilder::gableRoof(float x0, float z0, float x1, float z1, float y, float rise, bool ridgeAlongX, int layer, float tile, float ov) {
  x0 -= ov; x1 += ov; z0 -= ov; z1 += ov;
  float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
  Vec3 n;
  auto slope = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    // a,b along the eave (low), c,d along the ridge (high): CCW seen from above
    Vec3 nn = (b - a).cross(d - a).normalized();
    float lu = (b - a).length() / tile, lv = (d - a).length() / tile;
    quad(a, b, c, d, {0, lv}, {lu, lv}, {lu, 0}, {0, 0}, layer, 1, 1, 1, 1);
    (void)nn;
  };
  if (ridgeAlongX) {
    // slopes toward -z and +z
    slope({x0, y, z1}, {x1, y, z1}, {x1, y + rise, cz}, {x0, y + rise, cz});  // south slope (faces +z/up)
    slope({x1, y, z0}, {x0, y, z0}, {x0, y + rise, cz}, {x1, y + rise, cz});  // north slope
    // gable end triangles (wall-coloured) are closed by the wall meshes; add thin tri to hide gaps
  } else {
    slope({x1, y, z1}, {x1, y, z0}, {x0 + (x1 - x0) * 0.5f, y + rise, z0}, {cx, y + rise, z1});  // east slope
    slope({x0, y, z0}, {x0, y, z1}, {cx, y + rise, z1}, {cx, y + rise, z0});                      // west slope
  }
  (void)n;
}

// ------------------------------------------------------------------------------------------------ organic primitives
void MeshBuilder::tri(const Vec3& p0, const Vec3& n0, const Vec2& u0, const Vec3& p1, const Vec3& n1, const Vec2& u1, const Vec3& p2, const Vec3& n2,
                      const Vec2& u2, int layer, float ao0, float ao1, float ao2, const Vec3& outward) {
  bool flip = (p1 - p0).cross(p2 - p0).dot(outward) < 0;
  uint32_t i0 = vert(p0, n0, u0, layer, ao0);
  uint32_t i1 = vert(flip ? p2 : p1, flip ? n2 : n1, flip ? u2 : u1, layer, flip ? ao2 : ao1);
  uint32_t i2 = vert(flip ? p1 : p2, flip ? n1 : n2, flip ? u1 : u2, layer, flip ? ao1 : ao2);
  m_->idx.insert(m_->idx.end(), {i0, i1, i2});
}

void MeshBuilder::frustum(const Vec3& a, const Vec3& b, float r0, float r1, int sides, int layer, bool capTop, bool capBottom, float vTile) {
  Vec3 d = b - a;
  float len = d.length();
  if (len < 1e-5f) return;
  d = d / len;
  Vec3 h = std::fabs(d.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  Vec3 u = h.cross(d).normalized(), v = d.cross(u);
  float slope = (r0 - r1) / len;   // normal tilts toward the axis direction on a taper
  for (int i = 0; i < sides; ++i) {
    float t0 = (float)i / sides * kTau, t1 = (float)(i + 1) / sides * kTau;
    Vec3 w0 = u * std::cos(t0) + v * std::sin(t0), w1 = u * std::cos(t1) + v * std::sin(t1);
    Vec3 n0 = (w0 + d * slope).normalized(), n1 = (w1 + d * slope).normalized();
    Vec3 pa0 = a + w0 * r0, pa1 = a + w1 * r0, pb0 = b + w0 * r1, pb1 = b + w1 * r1;
    float uu0 = (float)i / sides * 2.0f, uu1 = (float)(i + 1) / sides * 2.0f, vv = len * vTile * 0.5f;
    Vec3 out = (w0 + w1) * 0.5f;
    tri(pa0, n0, {uu0, vv}, pa1, n1, {uu1, vv}, pb1, n1, {uu1, 0}, layer, 0.8f, 0.8f, 1.0f, out);
    tri(pa0, n0, {uu0, vv}, pb1, n1, {uu1, 0}, pb0, n0, {uu0, 0}, layer, 0.8f, 1.0f, 1.0f, out);
  }
  auto cap = [&](const Vec3& c, float r, const Vec3& nrm, float ao) {
    for (int i = 0; i < sides; ++i) {
      float t0 = (float)i / sides * kTau, t1 = (float)(i + 1) / sides * kTau;
      Vec3 p0 = c + (u * std::cos(t0) + v * std::sin(t0)) * r, p1 = c + (u * std::cos(t1) + v * std::sin(t1)) * r;
      tri(c, nrm, {0.5f, 0.5f}, p0, nrm, {0, 0}, p1, nrm, {1, 0}, layer, ao, ao, ao, nrm);
    }
  };
  if (capTop && r1 > 1e-4f) cap(b, r1, d, 1.0f);
  if (capBottom && r0 > 1e-4f) cap(a, r0, d * -1.0f, 0.6f);
}

static float hash3(int i, int j, uint32_t s) {
  uint32_t x = (uint32_t)(i * 73856093) ^ (uint32_t)(j * 19349663) ^ (s * 83492791u);
  x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15;
  return (x & 0xFFFF) / 65535.0f;
}

void MeshBuilder::blob(const Vec3& c, const Vec3& rad, int segs, int rings, float lump, uint32_t seed, int layer, float aoBase) {
  std::vector<Vec3> P((rings + 1) * (segs + 1)), N(P.size());
  std::vector<float> AO(P.size());
  for (int i = 0; i <= rings; ++i) {
    float ph = (float)i / rings * kPi;
    for (int j = 0; j <= segs; ++j) {
      int jj = j % segs;
      float th = (float)jj / segs * kTau;
      Vec3 dir{std::sin(ph) * std::cos(th), std::cos(ph), std::sin(ph) * std::sin(th)};
      // smooth 3D value noise over the direction (two octaves) so the crown gets soft lumps instead of per-vertex spikes;
      // the same value is used at the seam and the poles, so the surface stays closed
      auto vnoise = [&](Vec3 q, uint32_t sd) {
        float fx = std::floor(q.x), fy = std::floor(q.y), fz = std::floor(q.z);
        float tx = q.x - fx, ty = q.y - fy, tz = q.z - fz;
        tx = tx * tx * (3.0f - 2.0f * tx); ty = ty * ty * (3.0f - 2.0f * ty); tz = tz * tz * (3.0f - 2.0f * tz);
        auto h = [&](int dx, int dy, int dz) { return hash3((int)fx + dx, (int)fy + dy * 131 + dz * 7919, sd); };
        float c00 = lerp(h(0, 0, 0), h(1, 0, 0), tx), c10 = lerp(h(0, 1, 0), h(1, 1, 0), tx);
        float c01 = lerp(h(0, 0, 1), h(1, 0, 1), tx), c11 = lerp(h(0, 1, 1), h(1, 1, 1), tx);
        return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
      };
      float k = 0.65f * vnoise(dir * 1.7f + Vec3{3.1f, 1.3f, 7.7f}, seed) + 0.35f * vnoise(dir * 4.1f + Vec3{9.0f, 2.2f, 5.4f}, seed ^ 0x9e3779b9u);
      float r = 1.0f + lump * (k - 0.5f) * 2.4f;
      P[i * (segs + 1) + j] = c + Vec3{dir.x * rad.x, dir.y * rad.y, dir.z * rad.z} * r;
      N[i * (segs + 1) + j] = Vec3{dir.x / rad.x, dir.y / rad.y, dir.z / rad.z}.normalized();
      AO[i * (segs + 1) + j] = aoBase + (1.0f - aoBase) * (0.5f + 0.5f * dir.y);
    }
  }
  for (int i = 0; i < rings; ++i)
    for (int j = 0; j < segs; ++j) {
      int a = i * (segs + 1) + j, b = a + 1, c2 = a + segs + 1, d = c2 + 1;
      Vec2 ua{(float)j / segs * 4.0f, (float)i / rings * 3.0f}, ub{(float)(j + 1) / segs * 4.0f, ua.y}, uc{ua.x, (float)(i + 1) / rings * 3.0f},
          ud{ub.x, uc.y};
      Vec3 out = (N[a] + N[d]).normalized();
      if (i > 0) tri(P[a], N[a], ua, P[c2], N[c2], uc, P[b], N[b], ub, layer, AO[a], AO[c2], AO[b], out);
      if (i < rings - 1) tri(P[b], N[b], ub, P[c2], N[c2], uc, P[d], N[d], ud, layer, AO[b], AO[c2], AO[d], out);
    }
}

void MeshBuilder::frond(const Vec3& base, const Vec3& dir, float len, float width, float droop, int segs, int layer) {
  Vec3 d = dir.normalized();
  Vec3 side = Vec3{0, 1, 0}.cross(d);
  if (side.length() < 1e-3f) side = Vec3{1, 0, 0};
  side = side.normalized();
  Vec3 prevC = base, prevL = base, prevR = base;
  for (int s = 1; s <= segs; ++s) {
    float t = (float)s / segs;
    // arc: starts along dir and bends down (quadratic droop)
    Vec3 c = base + d * (len * t) + Vec3{0, -droop * t * t, 0};
    float w = width * std::sin(clamp(t * 1.15f, 0.0f, 1.0f) * kPi * 0.62f + 0.25f) * (1.0f - 0.55f * t);
    // V-shaped cross section: the edges hang below the midrib
    Vec3 l = c - side * w + Vec3{0, -w * 0.45f, 0}, r = c + side * w + Vec3{0, -w * 0.45f, 0};
    float v0 = (float)(s - 1) / segs, v1 = t;
    Vec3 up{0, 1, 0};
    // left and right halves, both faces
    for (int face = 0; face < 2; ++face) {
      Vec3 nrm = face == 0 ? up : Vec3{0, -1, 0};
      tri(prevC, nrm, {0.5f, v0}, prevL, nrm, {0, v0}, l, nrm, {0, v1}, layer, 0.9f, 0.75f, 0.75f, nrm);
      tri(prevC, nrm, {0.5f, v0}, l, nrm, {0, v1}, c, nrm, {0.5f, v1}, layer, 0.9f, 0.75f, 1.0f, nrm);
      tri(prevC, nrm, {0.5f, v0}, c, nrm, {0.5f, v1}, r, nrm, {1, v1}, layer, 0.9f, 1.0f, 0.75f, nrm);
      tri(prevC, nrm, {0.5f, v0}, r, nrm, {1, v1}, prevR, nrm, {1, v0}, layer, 0.9f, 0.75f, 0.75f, nrm);
    }
    prevC = c; prevL = l; prevR = r;
  }
}

void MeshBuilder::quad2(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, int layer, const Vec2& uvs) {
  quad(a, b, c, d, {0, uvs.y}, {uvs.x, uvs.y}, {uvs.x, 0}, {0, 0}, layer);
  quad(a, d, c, b, {0, uvs.y}, {0, 0}, {uvs.x, 0}, {uvs.x, uvs.y}, layer);
}

}  // namespace gtabr
