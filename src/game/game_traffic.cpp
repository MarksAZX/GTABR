// Ambient traffic: civilian cars that cruise the generated street grid in their own (right-hand) lane, slow down for cars,
// pedestrians and the player ahead, honk when blocked, and are recycled far from the player so the streets always feel busy
// without simulating the whole city. They are real vehicles: the player can crash into them and they run over nobody on purpose.
#include <algorithm>
#include <cmath>

#include "../core/log.h"
#include "game.h"

namespace gtabr {

namespace {
// metres of lane offset from a street's centre line (half the carriageway, so the car sits in the middle of its lane)
float laneOffsetFor(const World& w, Vec2 a, Vec2 b) {
  bool horizontal = std::fabs(b.y - a.y) < std::fabs(b.x - a.x);
  Vec2 m = (a + b) * 0.5f;
  for (const RoadLine& r : w.roads)
    if (r.horizontal == horizontal && std::fabs(r.c - (horizontal ? m.y : m.x)) < 0.8f) return r.hw * 0.5f;
  return 2.6f;
}
Vec2 rightOf(Vec2 d) { return {-d.y, d.x}; }   // matches right2(yawFromDir(d)) in this game's axes
}  // namespace

void Game::spawnTraffic() {
  vehicles_.erase(std::remove_if(vehicles_.begin(), vehicles_.end(), [](const Vehicle& v) { return v.traffic; }), vehicles_.end());
  static const int kCount[4] = {4, 7, 10, 14};
  int n = kCount[clamp(settings_.quality, 0, 3)];
  for (int i = 0; i < n; ++i) spawnTrafficCar(false);
}

bool Game::spawnTrafficCar(bool farFromPlayer) {
  if (world_.roads.empty()) return false;
  for (int attempt = 0; attempt < 40; ++attempt) {
    const RoadLine& r = world_.roads[wRng_.irange(0, (int)world_.roads.size() - 1)];
    float t = wRng_.range(r.a + 14.0f, r.b - 14.0f);
    Vec2 centre = r.horizontal ? Vec2{t, r.c} : Vec2{r.c, t};
    Vec2 dir = r.horizontal ? Vec2{wRng_.chance(0.5f) ? 1.0f : -1.0f, 0} : Vec2{0, wRng_.chance(0.5f) ? 1.0f : -1.0f};
    Vec2 pos = centre + rightOf(dir) * (r.hw * 0.5f);
    float dp = (pos - player_.pos).length();
    if (farFromPlayer ? (dp < 85.0f || dp > 190.0f) : dp < 26.0f) continue;
    // keep away from every other car, the player's spawn and the specials' frontage
    bool clash = false;
    for (const Vehicle& o : vehicles_) if (!o.despawn && (o.pos - pos).length() < 9.0f) clash = true;
    for (const ParkedCarDef& p : world_.parked) if ((Vec2{p.pos.x, p.pos.z} - pos).length() < 4.5f) clash = true;
    if (clash) continue;
    // not on a junction mouth (cross traffic is not modelled there)
    bool junction = false;
    for (const RoadLine& o : world_.roads)
      if (o.horizontal != r.horizontal && std::fabs(o.c - (r.horizontal ? pos.x : pos.y)) < o.hw + 6.0f) junction = true;
    if (junction) continue;
    // reuse a recycled slot so every index (the player's seat, saves) stays stable
    int slot = -1;
    for (size_t i = 0; i < vehicles_.size(); ++i) if (vehicles_[i].traffic && vehicles_[i].despawn) { slot = (int)i; break; }
    Vehicle v;
    v.id = slot >= 0 ? slot : (int)vehicles_.size();
    v.model = wRng_.irange(0, 2);
    v.color = wRng_.irange(0, 2);
    v.pos = pos;
    v.yaw = yawFromDir(dir);
    v.traffic = true;
    v.engineOn = true;
    v.fuel = vehicleDef(v.model).fuelCap;
    v.health = 100;
    v.cruise = wRng_.range(7.5f, 12.0f);
    if (slot >= 0) vehicles_[slot] = v; else vehicles_.push_back(v);
    planTrafficRoute(vehicles_[v.id]);
    return true;
  }
  return false;
}

// Picks a destination on the grid and turns the centre-line route into a lane path with a point every few metres.
void Game::planTrafficRoute(Vehicle& v) {
  v.route.clear();
  v.routeIdx = 0;
  Vec2 here = v.pos;
  Vec2 dest = world_.nearestRoadPoint({wRng_.range(world_.land.x0, world_.land.x1), wRng_.range(world_.land.z0, world_.land.z1)});
  std::vector<Vec2> centre = world_.roadRoute(world_.nearestRoadPoint(here), dest);
  // remove duplicates
  std::vector<Vec2> pts;
  for (const Vec2& p : centre) if (pts.empty() || (p - pts.back()).length() > 1.0f) pts.push_back(p);
  if (pts.size() < 2) { pts.clear(); pts.push_back(world_.nearestRoadPoint(here)); pts.push_back(world_.nearestRoadPoint(here) + Vec2{1, 0}); }
  // offset every vertex into the right lane (the intersection of the offset incoming and outgoing lane lines)
  std::vector<Vec2> lane(pts.size());
  for (size_t i = 0; i < pts.size(); ++i) {
    Vec2 in = i > 0 ? (pts[i] - pts[i - 1]).normalized() : Vec2{0, 0}, out = i + 1 < pts.size() ? (pts[i + 1] - pts[i]).normalized() : Vec2{0, 0};
    Vec2 o{0, 0};
    if (i > 0) o += rightOf(in) * laneOffsetFor(world_, pts[i - 1], pts[i]);
    if (i + 1 < pts.size()) o += rightOf(out) * laneOffsetFor(world_, pts[i], pts[i + 1]);
    lane[i] = pts[i] + o;
  }
  // start from the car's own lane position, then densify
  v.route.push_back(here);
  for (size_t i = 0; i < lane.size(); ++i) {
    Vec2 a = v.route.back(), b = lane[i];
    float d = (b - a).length();
    // skip lane points that are behind the car
    if (i + 1 < lane.size() && d < 6.0f) continue;
    int n = std::max(1, (int)std::ceil(d / 8.0f));
    for (int k = 1; k <= n; ++k) v.route.push_back(lerp(a, b, (float)k / n));
  }
}

void Game::updateTraffic(float dt) {
  if (phase_ != Phase::Playing) return;
  const Vec2 pp = player_.vehicle >= 0 ? vehicles_[player_.vehicle].pos : player_.pos;
  int recycled = 0;
  for (size_t idx = 0; idx < vehicles_.size(); ++idx) {
    Vehicle& v = vehicles_[idx];
    if (!v.traffic || v.despawn) continue;
    float dist = (v.pos - pp).length();
    // recycle cars that are far away (or wrecked and out of sight) so the density follows the player
    if (dist > 210.0f || (v.wrecked && dist > 60.0f)) {
      if (recycled < 1 && !player_.indoors) {
        v.despawn = true;
        v.pos = {9999, 9999};
        ++recycled;
      }
      continue;
    }
    if (v.wrecked) { VehicleInput none; none.handbrake = true; stepVehicle(v, none, dt, world_, vehicles_); continue; }
    if (v.route.empty() || v.routeIdx >= v.route.size()) planTrafficRoute(v);
    // pure-pursuit target a few metres ahead on the lane path
    float look = 5.0f + std::fabs(v.speed) * 0.45f;
    while (v.routeIdx + 1 < v.route.size() && (v.route[v.routeIdx] - v.pos).length() < look * 0.6f) ++v.routeIdx;
    Vec2 target = v.route[std::min(v.routeIdx + 1, v.route.size() - 1)];
    Vec2 d = target - v.pos;
    float err = wrapAngle(yawFromDir(d) - v.yaw);
    // what is in front? (cars, the player, pedestrians) within a lane-wide corridor
    Vec2 fwd = fwd2(v.yaw), rgt = right2(v.yaw);
    const VehicleDef& vd = vehicleDef(v.model);
    float seeDist = 7.0f + std::fabs(v.speed) * 1.1f, nearest = 1e9f;
    auto consider = [&](Vec2 p, float halfWidth) {
      Vec2 rel = p - v.pos;
      float f = rel.dot(fwd) - vd.length * 0.5f, l = std::fabs(rel.dot(rgt));
      if (f > -0.5f && f < seeDist && l < halfWidth + vd.width * 0.5f) nearest = std::min(nearest, std::max(0.0f, f));
    };
    for (const Vehicle& o : vehicles_) {
      if (&o == &v || o.despawn) continue;
      const VehicleDef& od = vehicleDef(o.model);
      consider(o.pos, od.width * 0.5f + 0.5f);
    }
    for (const Npc& n : npcs_)
      if (!n.interior && !n.despawn && n.state != NpcState::Dead && !n.police) consider(n.pos, 0.9f);
    if (player_.vehicle < 0 && !player_.indoors && !player_.dead) consider(player_.pos, 1.1f);
    for (const ParkedCarDef& p : world_.parked) consider({p.pos.x, p.pos.z}, 1.0f);
    float want = v.cruise * clamp(1.0f - std::fabs(err) / 1.25f, 0.28f, 1.0f);
    if (nearest < seeDist) want = std::min(want, std::max(0.0f, (nearest - 3.2f) * 0.9f));
    VehicleInput in;
    if (v.aiReverse > 0) {
      v.aiReverse -= dt;
      in.throttle = -0.7f;
      in.steer = err > 0 ? -1.0f : 1.0f;
    } else {
      in.steer = clamp(err * 2.3f, -1.0f, 1.0f);
      in.throttle = clamp((want - v.speed) * 0.5f, -1.0f, 1.0f);
      in.handbrake = want < 0.1f && std::fabs(v.speed) < 0.8f;
    }
    bool blocked = nearest < 4.5f && std::fabs(v.speed) < 0.6f;
    v.blockedT = blocked ? v.blockedT + dt : std::max(0.0f, v.blockedT - dt * 2.0f);
    v.honkT = std::max(0.0f, v.honkT - dt);
    if (v.blockedT > 2.2f && v.honkT <= 0 && dist < 60.0f) { audio_.play("horn", {v.pos.x, 1.0f, v.pos.y}, 0.8f, wRng_.range(0.9f, 1.15f)); v.honkT = 3.0f; }
    if (v.blockedT > 9.0f) { v.blockedT = 0; v.aiReverse = 1.3f; planTrafficRoute(v); }
    // stuck against a wall / corner while pushing: back up and re-plan
    if (!blocked && in.throttle > 0.3f && std::fabs(v.speed) < 0.4f) v.aiStuck += dt; else v.aiStuck = std::max(0.0f, v.aiStuck - dt);
    if (v.aiStuck > 1.6f) { v.aiReverse = 1.1f; v.aiStuck = 0; planTrafficRoute(v); }
    v.engineOn = true;
    v.braking = in.throttle < -0.05f || want < v.speed - 1.0f;
    float impact = stepVehicle(v, in, dt, world_, vehicles_);
    if (impact > 6.0f) audio_.play("crash", {v.pos.x, 0.8f, v.pos.y}, clamp(impact / 14.0f, 0.3f, 0.9f));
    aiCarContacts(v);
  }
  // refill: recycled / destroyed cars come back in a street 90-190 m away from the player
  static const int kCount[4] = {4, 7, 10, 14};
  int target = kCount[clamp(settings_.quality, 0, 3)];
  int live = 0;
  for (const Vehicle& v : vehicles_) if (v.traffic && !v.despawn) ++live;
  if (live < target && !player_.indoors) {
    trafficRespawnT_ -= dt;
    if (trafficRespawnT_ <= 0) {
      spawnTrafficCar(true);
      trafficRespawnT_ = 1.2f;
    }
  }
}

}  // namespace gtabr
