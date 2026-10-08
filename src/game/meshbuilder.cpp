#include "meshbuilder.h"

#include <cmath>

namespace gtabr {

uint32_t MeshBuilder::vert(const Vec3& p, const Vec3& n, const Vec2& uv, int layer, float ao) {
  gfx::WorldVertex w{};
  w.p[0] = p.x; w.p[1] = p.y; w.p[2] = p.z;
  w.n[0] = (int8_t)std::lround(n.x * 127); w.n[1] = (int8_t)std::lround(n.y * 127); w.n[2] = (int8_t)std::lround(n.z * 127);
  w.uv[0] = uv.x; w.uv[1] = uv.y;
  w.color = packRGBA8(tint_.x, tint_.y, tint_.z, ao);
  w.layer = (float)layer;
  m_->v.push_back(w);
  m_->bounds.expand(p);
  if(layer==31){m_->bounds.expand({p.x,p.y-0.32f,p.z});m_->bounds.expand({p.x,p.y+0.32f,p.z});}
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

void MeshBuilder::tube(Vec3 a,Vec3 b,float radius,int sides,int layer){
  Vec3 axis=(b-a).normalized(),u=axis.cross(std::fabs(axis.y)>0.9f?Vec3{1,0,0}:Vec3{0,1,0}).normalized(),v=axis.cross(u);
  for(int i=0;i<sides;++i){float t=i*kTau/sides,t1=(i+1)*kTau/sides;Vec3 p=u*std::cos(t)+v*std::sin(t),q=u*std::cos(t1)+v*std::sin(t1);quad(a+p*radius,a+q*radius,b+q*radius,b+p*radius,{0,0},{1,0},{1,1},{0,1},layer);}
}
}  // namespace gtabr
