#include "physics.h"

#include <algorithm>
#include <cmath>

namespace gtabr {
namespace phys {

namespace {
void obbAxes(const OBB& o, Vec2& ax, Vec2& ay) {
  ax = right2(o.yaw);
  ay = fwd2(o.yaw);
}
void obbCorners(const OBB& o, Vec2 out[4]) {
  Vec2 ax, ay;
  obbAxes(o, ax, ay);
  out[0] = o.c + ax * o.half.x + ay * o.half.y;
  out[1] = o.c - ax * o.half.x + ay * o.half.y;
  out[2] = o.c - ax * o.half.x - ay * o.half.y;
  out[3] = o.c + ax * o.half.x - ay * o.half.y;
}
void project(const Vec2 pts[4], Vec2 axis, float& mn, float& mx) {
  mn = mx = pts[0].dot(axis);
  for (int i = 1; i < 4; ++i) {
    float d = pts[i].dot(axis);
    mn = std::min(mn, d); mx = std::max(mx, d);
  }
}
}  // namespace

Hit obbVsObb(const OBB& a, const OBB& b) {
  Hit h;
  Vec2 pa[4], pb[4];
  obbCorners(a, pa);
  obbCorners(b, pb);
  Vec2 axes[4];
  obbAxes(a, axes[0], axes[1]);
  obbAxes(b, axes[2], axes[3]);
  float best = 1e30f;
  Vec2 bestN;
  for (int i = 0; i < 4; ++i) {
    float mnA, mxA, mnB, mxB;
    project(pa, axes[i], mnA, mxA);
    project(pb, axes[i], mnB, mxB);
    float overlap = std::min(mxA, mxB) - std::max(mnA, mnB);
    if (overlap <= 0) return h;
    if (overlap < best) {
      best = overlap;
      float dir = (a.c - b.c).dot(axes[i]);
      bestN = dir >= 0 ? axes[i] : -axes[i];
    }
  }
  h.hit = true;
  h.depth = best;
  h.normal = bestN;
  return h;
}

Hit obbVsAabb(const OBB& a, const AABB& b) {
  OBB bb;
  bb.c = {(b.mn.x + b.mx.x) * 0.5f, (b.mn.z + b.mx.z) * 0.5f};
  bb.half = {(b.mx.x - b.mn.x) * 0.5f, (b.mx.z - b.mn.z) * 0.5f};
  bb.yaw = kPi * 0.5f;  // makes the 'across' axis = +z... any orientation is fine for an axis-aligned box
  // For yaw = pi/2: right2 = (0,1) (z), fwd2 = (1,0) (x): half.x applies to z, half.y to x -> swap.
  bb.half = {bb.half.y, bb.half.x};
  return obbVsObb(a, bb);
}

Hit circleVsObb(Vec2 c, float r, const OBB& b) {
  Hit h;
  Vec2 ax, ay;
  obbAxes(b, ax, ay);
  Vec2 d = c - b.c;
  float lx = d.dot(ax), ly = d.dot(ay);
  float cx = clamp(lx, -b.half.x, b.half.x), cy = clamp(ly, -b.half.y, b.half.y);
  float dx = lx - cx, dy = ly - cy;
  float dist2 = dx * dx + dy * dy;
  if (dist2 >= r * r) return h;
  if (dist2 > 1e-8f) {
    float dist = std::sqrt(dist2);
    Vec2 n = (ax * dx + ay * dy) / dist;
    h.hit = true; h.normal = n; h.depth = r - dist;
  } else {
    // centre inside the box: push out through the nearest face
    float px = b.half.x - std::fabs(lx), py = b.half.y - std::fabs(ly);
    if (px < py) h.normal = ax * (lx >= 0 ? 1.0f : -1.0f), h.depth = px + r;
    else h.normal = ay * (ly >= 0 ? 1.0f : -1.0f), h.depth = py + r;
    h.hit = true;
  }
  return h;
}

static bool resolveCircleStatic(const World& w, Vec2& pos, float radius, bool collideCars, float maxY, std::vector<int>& tmp, float feetY = -1e9f) {
  bool touched = false;
  w.queryColliders(pos.x - radius - 0.1f, pos.y - radius - 0.1f, pos.x + radius + 0.1f, pos.y + radius + 0.1f, tmp);
  for (int id : tmp) {
    const Collider& c = w.colliders[id];
    if (!collideCars && c.kind == ColKind::Car) continue;
    if (c.box.mn.y > maxY) continue;
    if (c.box.mx.y < feetY) continue;   // the feet are above this obstacle (vaulting)
    float cx = clamp(pos.x, c.box.mn.x, c.box.mx.x), cz = clamp(pos.y, c.box.mn.z, c.box.mx.z);
    float dx = pos.x - cx, dz = pos.y - cz;
    float d2 = dx * dx + dz * dz;
    if (d2 >= radius * radius) continue;
    touched = true;
    if (d2 > 1e-10f) {
      float d = std::sqrt(d2);
      pos.x += dx / d * (radius - d);
      pos.y += dz / d * (radius - d);
    } else {
      // centre is inside: exit via the nearest face
      float l = pos.x - c.box.mn.x, r = c.box.mx.x - pos.x, t = pos.y - c.box.mn.z, b = c.box.mx.z - pos.y;
      float m = std::min(std::min(l, r), std::min(t, b));
      if (m == l) pos.x = c.box.mn.x - radius;
      else if (m == r) pos.x = c.box.mx.x + radius;
      else if (m == t) pos.y = c.box.mn.z - radius;
      else pos.y = c.box.mx.z + radius;
    }
  }
  return touched;
}

bool moveCircle(const World& w, Vec2& pos, Vec2 delta, float radius, bool collideCars, float maxY, float feetY) {
  float len = delta.length();
  int steps = std::max(1, (int)std::ceil(len / 0.2f));
  Vec2 step = delta / (float)steps;
  bool touched = false;
  std::vector<int> tmp;
  tmp.reserve(32);
  for (int i = 0; i < steps; ++i) {
    pos += step;
    for (int it = 0; it < 3; ++it)
      if (resolveCircleStatic(w, pos, radius, collideCars, maxY, tmp, feetY)) touched = true;
      else break;
  }
  return touched;
}

void depenetrateCircle(const World& w, Vec2& pos, float radius) {
  std::vector<int> tmp;
  for (int it = 0; it < 6; ++it)
    if (!resolveCircleStatic(w, pos, radius, true, 3.0f, tmp)) break;
}

float raycast(const World& w, Vec2 from, Vec2 dir, float maxDist, float height) {
  float best = maxDist;
  std::vector<int> tmp;
  Vec2 to = from + dir * maxDist;
  w.queryColliders(std::min(from.x, to.x) - 1, std::min(from.y, to.y) - 1, std::max(from.x, to.x) + 1, std::max(from.y, to.y) + 1, tmp);
  for (int id : tmp) {
    const Collider& c = w.colliders[id];
    if (c.box.mx.y < height * 0.5f && c.kind != ColKind::Wall && c.kind != ColKind::Building) continue;
    if (c.kind == ColKind::Pole || c.kind == ColKind::Tree || c.kind == ColKind::Car) continue;
    if (c.box.mx.y < height) continue;  // the ray passes above short obstacles
    // slab test
    float t0 = 0, t1 = best;
    bool ok = true;
    for (int a = 0; a < 2 && ok; ++a) {
      float o = a == 0 ? from.x : from.y, d = a == 0 ? dir.x : dir.y;
      float mn = a == 0 ? c.box.mn.x : c.box.mn.z, mx = a == 0 ? c.box.mx.x : c.box.mx.z;
      if (std::fabs(d) < 1e-8f) { if (o < mn || o > mx) ok = false; }
      else {
        float ta = (mn - o) / d, tb = (mx - o) / d;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta); t1 = std::min(t1, tb);
        if (t0 > t1) ok = false;
      }
    }
    if (ok && t0 < best) best = std::max(0.0f, t0);
  }
  return best;
}

bool segmentBlocked(const World& w, Vec2 a, Vec2 b, float radius) {
  Vec2 d = b - a;
  float len = d.length();
  if (len < 1e-4f) return false;
  int steps = (int)std::ceil(len / 0.4f);
  std::vector<int> tmp;
  for (int i = 0; i <= steps; ++i) {
    Vec2 p = a + d * ((float)i / steps);
    w.queryColliders(p.x - radius, p.y - radius, p.x + radius, p.y + radius, tmp);
    for (int id : tmp) {
      const Collider& c = w.colliders[id];
      if (c.kind == ColKind::Car) continue;
      float cx = clamp(p.x, c.box.mn.x, c.box.mx.x), cz = clamp(p.y, c.box.mn.z, c.box.mx.z);
      if ((p.x - cx) * (p.x - cx) + (p.y - cz) * (p.y - cz) < radius * radius) return true;
    }
  }
  return false;
}

}  // namespace phys
}  // namespace gtabr
