// Navigation mesh built from walkable rectangles minus inflated obstacles.
// The grid is greedily merged into rectangular convex polygons; paths are found with A* over the polygon
// adjacency graph (portal midpoints) and then straightened with grid line-of-sight checks.
#pragma once
#include <vector>

#include "../core/util.h"
#include "world.h"

namespace gtabr {

class NavMesh {
 public:
  void build(const RectF& bounds, float cell, const std::vector<RectF>& walkable, const std::vector<RectF>& blockers, float agentRadius);
  bool findPath(Vec2 from, Vec2 to, std::vector<Vec2>& out) const;
  bool randomPoint(Rng& rng, Vec2& out) const;
  bool nearestWalkable(Vec2 p, Vec2& out) const;
  bool isWalkable(Vec2 p) const;
  size_t polyCount() const { return polys_.size(); }
  const RectF& bounds() const { return bounds_; }
  // Test helper: true if the straight segment stays on walkable cells.
  bool lineOfSight(Vec2 a, Vec2 b) const;

 private:
  struct Poly { RectF r; std::vector<int> nbrs; float area = 0; };
  int cellIndex(Vec2 p) const;
  bool cellWalkable(int cx, int cz) const { return cx >= 0 && cz >= 0 && cx < w_ && cz < h_ && walk_[(size_t)cz * w_ + cx]; }
  RectF bounds_;
  float cell_ = 0.5f;
  int w_ = 0, h_ = 0;
  std::vector<uint8_t> walk_;
  std::vector<int> polyOf_;
  std::vector<Poly> polys_;
  std::vector<float> cumArea_;
};

}  // namespace gtabr
