// CPU-side geometry builder for the static world (ground, buildings, roofs, poles, interiors).
#pragma once
#include <vector>

#include "../gfx/renderer.h"

namespace gtabr {

struct MeshData {
  std::vector<gfx::WorldVertex> v;
  std::vector<uint32_t> idx;
  AABB bounds;
  void clear() { v.clear(); idx.clear(); bounds = AABB(); }
  bool empty() const { return idx.empty(); }
};

class MeshBuilder {
 public:
  explicit MeshBuilder(MeshData* m) : m_(m) {}
  void setTint(const Vec3& t) { tint_ = t; }
  Vec3 tint() const { return tint_; }

  // Quad given counter-clockwise when seen from the front (normal side). uv per corner.
  void quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec2& ua, const Vec2& ub, const Vec2& uc,
            const Vec2& ud, int layer, float aoA = 1, float aoB = 1, float aoC = 1, float aoD = 1);
  // Horizontal top face with world-space tiling (tile = metres per texture repeat).
  void groundRect(float x0, float z0, float x1, float z1, float y, int layer, float tile, float uOff = 0, float vOff = 0);
  void roofRect(float x0, float z0, float x1, float z1, float y, int layer, float tile);
  // Vertical wall from (x0,z0) to (x1,z1) (left -> right as seen from the outside), bottom y0, top y1.
  void wall(float x0, float z0, float x1, float z1, float y0, float y1, int layer, float u0, float u1, float v0, float v1,
            float aoBottom = 0.78f, float aoTop = 1.0f);
  // Axis aligned box. Faces use layerSide (walls), layerTop (top), world-space tiling for UVs.
  void box(const AABB& b, int layerSide, int layerTop, float tile, bool bottom = false);
  void prism(Vec3 base, float radius, float height, int sides, int layer, float tile = 2.0f);  // vertical n-gon pole
  void gableRoof(float x0, float z0, float x1, float z1, float y, float rise, bool ridgeAlongX, int layer, float tile, float overhang);
  void ceiling(float x0, float z0, float x1, float z1, float y, int layer, float tile);  // downward facing

  void tube(Vec3 a,Vec3 b,float radius,int sides,int layer);

  MeshData* mesh() { return m_; }

 private:
  uint32_t vert(const Vec3& p, const Vec3& n, const Vec2& uv, int layer, float ao);
  MeshData* m_;
  Vec3 tint_{1, 1, 1};
};

}  // namespace gtabr
