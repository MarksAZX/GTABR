// 2D (XZ plane) collision helpers on top of the 3D collider boxes: circle sliding, OBB-vs-AABB / OBB-vs-OBB SAT.
#pragma once
#include "world.h"

namespace gtabr {
namespace phys {

struct OBB {
  Vec2 c;       // centre
  Vec2 half;    // half extents (x = across, y = along the heading)
  float yaw = 0;
};

struct Hit {
  bool hit = false;
  Vec2 normal;   // pushes object A out of B
  float depth = 0;
};

Hit obbVsAabb(const OBB& a, const AABB& b);
Hit obbVsObb(const OBB& a, const OBB& b);
Hit circleVsObb(Vec2 c, float r, const OBB& b);   // normal pushes the circle out of the box

// Moves a circle through the static world sliding along colliders. Returns true if it touched something.
bool moveCircle(const World& w, Vec2& pos, Vec2 delta, float radius, bool collideCars = true, float maxY = 3.0f);
// Pushes a circle out of static geometry without moving it further (e.g. after teleports).
void depenetrateCircle(const World& w, Vec2& pos, float radius);
// Ray (2D) vs colliders, used for the third-person camera. Returns the distance in [0,maxDist].
float raycast(const World& w, Vec2 from, Vec2 dir, float maxDist, float height);
bool segmentBlocked(const World& w, Vec2 a, Vec2 b, float radius);

}  // namespace phys
}  // namespace gtabr
