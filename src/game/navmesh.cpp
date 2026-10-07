#include "navmesh.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace gtabr {

void NavMesh::build(const RectF& bounds, float cell, const std::vector<RectF>& walkable, const std::vector<RectF>& blockers, float radius) {
  bounds_ = bounds;
  cell_ = cell;
  w_ = (int)std::ceil((bounds.x1 - bounds.x0) / cell);
  h_ = (int)std::ceil((bounds.z1 - bounds.z0) / cell);
  walk_.assign((size_t)w_ * h_, 0);
  polyOf_.assign((size_t)w_ * h_, -1);
  polys_.clear();
  auto rasterize = [&](const RectF& r, uint8_t value) {
    int x0 = std::max(0, (int)std::floor((r.x0 - bounds.x0) / cell)), x1 = std::min(w_ - 1, (int)std::floor((r.x1 - bounds.x0) / cell));
    int z0 = std::max(0, (int)std::floor((r.z0 - bounds.z0) / cell)), z1 = std::min(h_ - 1, (int)std::floor((r.z1 - bounds.z0) / cell));
    for (int z = z0; z <= z1; ++z)
      for (int x = x0; x <= x1; ++x) {
        float cx = bounds.x0 + (x + 0.5f) * cell, cz = bounds.z0 + (z + 0.5f) * cell;
        if (r.contains(cx, cz)) walk_[(size_t)z * w_ + x] = value;
      }
  };
  for (const RectF& r : walkable) rasterize(r, 1);
  for (const RectF& r : blockers) rasterize(r.inflated(radius), 0);

  // greedy rectangle merge
  for (int z = 0; z < h_; ++z)
    for (int x = 0; x < w_; ++x) {
      size_t i = (size_t)z * w_ + x;
      if (!walk_[i] || polyOf_[i] >= 0) continue;
      int x1 = x;
      while (x1 + 1 < w_ && walk_[(size_t)z * w_ + x1 + 1] && polyOf_[(size_t)z * w_ + x1 + 1] < 0) ++x1;
      int z1 = z;
      for (;;) {
        int nz = z1 + 1;
        if (nz >= h_) break;
        bool ok = true;
        for (int xx = x; xx <= x1 && ok; ++xx) ok = walk_[(size_t)nz * w_ + xx] && polyOf_[(size_t)nz * w_ + xx] < 0;
        if (!ok) break;
        z1 = nz;
      }
      int id = (int)polys_.size();
      for (int zz = z; zz <= z1; ++zz)
        for (int xx = x; xx <= x1; ++xx) polyOf_[(size_t)zz * w_ + xx] = id;
      Poly p;
      p.r = {bounds.x0 + x * cell, bounds.z0 + z * cell, bounds.x0 + (x1 + 1) * cell, bounds.z0 + (z1 + 1) * cell};
      p.area = (p.r.x1 - p.r.x0) * (p.r.z1 - p.r.z0);
      polys_.push_back(p);
    }
  // adjacency: walk the perimeter of each polygon
  for (int id = 0; id < (int)polys_.size(); ++id) {
    Poly& p = polys_[id];
    int x0 = (int)std::lround((p.r.x0 - bounds.x0) / cell), x1 = (int)std::lround((p.r.x1 - bounds.x0) / cell) - 1;
    int z0 = (int)std::lround((p.r.z0 - bounds.z0) / cell), z1 = (int)std::lround((p.r.z1 - bounds.z0) / cell) - 1;
    auto add = [&](int cx, int cz) {
      if (cx < 0 || cz < 0 || cx >= w_ || cz >= h_) return;
      int o = polyOf_[(size_t)cz * w_ + cx];
      if (o >= 0 && o != id && std::find(p.nbrs.begin(), p.nbrs.end(), o) == p.nbrs.end()) p.nbrs.push_back(o);
    };
    for (int x = x0; x <= x1; ++x) { add(x, z0 - 1); add(x, z1 + 1); }
    for (int z = z0; z <= z1; ++z) { add(x0 - 1, z); add(x1 + 1, z); }
  }
  cumArea_.clear();
  float acc = 0;
  for (auto& p : polys_) { acc += p.area; cumArea_.push_back(acc); }
}

int NavMesh::cellIndex(Vec2 p) const {
  int cx = (int)std::floor((p.x - bounds_.x0) / cell_), cz = (int)std::floor((p.y - bounds_.z0) / cell_);
  if (cx < 0 || cz < 0 || cx >= w_ || cz >= h_) return -1;
  return cz * w_ + cx;
}

bool NavMesh::isWalkable(Vec2 p) const {
  int i = cellIndex(p);
  return i >= 0 && walk_[(size_t)i];
}

bool NavMesh::nearestWalkable(Vec2 p, Vec2& out) const {
  if (isWalkable(p)) { out = p; return true; }
  int cx = (int)std::floor((p.x - bounds_.x0) / cell_), cz = (int)std::floor((p.y - bounds_.z0) / cell_);
  for (int r = 1; r < 40; ++r) {
    float best = 1e30f;
    bool found = false;
    for (int dz = -r; dz <= r; ++dz)
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
        if (!cellWalkable(cx + dx, cz + dz)) continue;
        Vec2 c{bounds_.x0 + (cx + dx + 0.5f) * cell_, bounds_.z0 + (cz + dz + 0.5f) * cell_};
        float d = (c - p).lengthSq();
        if (d < best) { best = d; out = c; found = true; }
      }
    if (found) return true;
  }
  return false;
}

bool NavMesh::randomPoint(Rng& rng, Vec2& out) const {
  if (polys_.empty()) return false;
  float t = rng.uni() * cumArea_.back();
  size_t i = std::lower_bound(cumArea_.begin(), cumArea_.end(), t) - cumArea_.begin();
  if (i >= polys_.size()) i = polys_.size() - 1;
  const RectF& r = polys_[i].r;
  out = {rng.range(r.x0 + 0.2f, r.x1 - 0.2f), rng.range(r.z0 + 0.2f, r.z1 - 0.2f)};
  return true;
}

bool NavMesh::lineOfSight(Vec2 a, Vec2 b) const {
  Vec2 d = b - a;
  float len = d.length();
  int steps = std::max(1, (int)std::ceil(len / (cell_ * 0.5f)));
  for (int i = 0; i <= steps; ++i)
    if (!isWalkable(a + d * ((float)i / steps))) return false;
  return true;
}

bool NavMesh::findPath(Vec2 from, Vec2 to, std::vector<Vec2>& out) const {
  out.clear();
  Vec2 s, g;
  if (!nearestWalkable(from, s) || !nearestWalkable(to, g)) return false;
  int ps = polyOf_[(size_t)cellIndex(s)], pg = polyOf_[(size_t)cellIndex(g)];
  if (ps < 0 || pg < 0) return false;
  if (ps == pg || lineOfSight(s, g)) { out.push_back(g); return true; }
  struct Node { float f; int id; bool operator<(const Node& o) const { return f > o.f; } };
  std::vector<float> gc(polys_.size(), 1e30f);
  std::vector<int> prev(polys_.size(), -1);
  std::priority_queue<Node> open;
  auto centre = [&](int i) { return Vec2{polys_[i].r.cx(), polys_[i].r.cz()}; };
  gc[ps] = 0;
  open.push({(centre(ps) - g).length(), ps});
  std::vector<uint8_t> closed(polys_.size(), 0);
  while (!open.empty()) {
    Node n = open.top();
    open.pop();
    if (closed[n.id]) continue;
    closed[n.id] = 1;
    if (n.id == pg) break;
    for (int nb : polys_[n.id].nbrs) {
      if (closed[nb]) continue;
      float c = gc[n.id] + (centre(n.id) - centre(nb)).length();
      if (c < gc[nb]) {
        gc[nb] = c;
        prev[nb] = n.id;
        open.push({c + (centre(nb) - g).length(), nb});
      }
    }
  }
  if (prev[pg] < 0) return false;
  std::vector<int> chain;
  for (int c = pg; c >= 0; c = prev[c]) chain.push_back(c);
  std::reverse(chain.begin(), chain.end());
  // waypoints: midpoint of the shared border between consecutive polygons
  std::vector<Vec2> pts;
  pts.push_back(s);
  for (size_t i = 0; i + 1 < chain.size(); ++i) {
    const RectF& a = polys_[chain[i]].r;
    const RectF& b = polys_[chain[i + 1]].r;
    float ox0 = std::max(a.x0, b.x0), ox1 = std::min(a.x1, b.x1), oz0 = std::max(a.z0, b.z0), oz1 = std::min(a.z1, b.z1);
    pts.push_back({(ox0 + ox1) * 0.5f, (oz0 + oz1) * 0.5f});
  }
  pts.push_back(g);
  // string-pull with line of sight
  size_t i = 0;
  out.clear();
  while (i + 1 < pts.size()) {
    size_t j = pts.size() - 1;
    while (j > i + 1 && !lineOfSight(pts[i], pts[j])) --j;
    out.push_back(pts[j]);
    i = j;
  }
  return true;
}

}  // namespace gtabr
