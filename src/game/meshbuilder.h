// CPU-side geometry builder for the static world (ground, buildings, roofs, poles, interiors).
#pragma once
#include <cmath>
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
  // Local -> world transform for everything emitted afterwards: yaw 0 keeps local +z facing world +z, positive yaw turns toward +x.
  void setXform(const Vec3& pos, float yaw, float scale = 1.0f) { xf_ = true; pos_ = pos; cy_ = std::cos(yaw); sy_ = std::sin(yaw); sc_ = scale; }
  void clearXform() { xf_ = false; }
  void setEmissive(float e) { emissive_ = e; }   // self-illumination strength (night lamps, signs, traffic lights)
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

  // ---- organic / round primitives (props, vegetation)
  // Tapered tube between two points (trunks, branches, poles, wires). Smooth radial normals; optional end caps.
  void frustum(const Vec3& a, const Vec3& b, float r0, float r1, int sides, int layer, bool capTop = true, bool capBottom = false, float vTile = 1.0f);
  // Lumpy ellipsoid (tree crowns, bushes). 'lump' = radial noise amount, aoBase = darkening at the bottom.
  void blob(const Vec3& c, const Vec3& radii, int segs, int rings, float lump, uint32_t seed, int layer, float aoBase = 0.55f);
  // Double-sided curved leaf / palm frond: starts at 'base', goes along 'dir' for len metres drooping by 'droop' at the tip.
  void frond(const Vec3& base, const Vec3& dir, float len, float width, float droop, int segs, int layer);
  // Single double-sided quad (cloth, signs, thin plates).
  void quad2(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, int layer, const Vec2& uvScale = {1, 1});

  MeshData* mesh() { return m_; }

 private:
  uint32_t vert(const Vec3& p, const Vec3& n, const Vec2& uv, int layer, float ao);
  void tri(const Vec3& p0, const Vec3& n0, const Vec2& u0, const Vec3& p1, const Vec3& n1, const Vec2& u1, const Vec3& p2, const Vec3& n2,
           const Vec2& u2, int layer, float ao0, float ao1, float ao2, const Vec3& outward);
  MeshData* m_;
  Vec3 tint_{1, 1, 1};
  bool xf_ = false;
  Vec3 pos_;
  float cy_ = 1, sy_ = 0, sc_ = 1;
  float emissive_ = 0;
};

}  // namespace gtabr
