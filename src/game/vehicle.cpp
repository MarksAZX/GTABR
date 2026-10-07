#include "entities.h"

#include <cmath>

namespace gtabr {

float stepVehicle(Vehicle& v, const VehicleInput& inIn, float dt, const World& w, const std::vector<Vehicle>& others) {
  const VehicleDef& d = vehicleDef(v.model);
  VehicleInput in = inIn;
  bool canDrive = v.fuel > 0.0f && v.engineOn;
  if (!canDrive) { in.throttle = 0; if (v.fuel <= 0.0f && v.engineOn) v.outOfFuelTimer += dt; }
  float healthFactor = v.health < 25.0f ? 0.72f : (v.health < 50.0f ? 0.9f : 1.0f);
  float vmax = d.maxSpeed * healthFactor;

  Vec2 fwd = fwd2(v.yaw), right = right2(v.yaw);
  float vF = v.vel.dot(fwd), vL = v.vel.dot(right);

  // ---- longitudinal
  float a = 0;
  if (in.throttle > 0.02f) {
    if (vF > -0.5f) {
      float ratio = clamp(vF / vmax, 0.0f, 1.0f);
      a = d.accel * in.throttle * (1.0f - ratio * ratio);
    } else a = d.brake * in.throttle;  // braking while rolling backwards
  } else if (in.throttle < -0.02f) {
    if (vF > 0.5f) a = -d.brake * -in.throttle;
    else {
      float ratio = clamp(-vF / (vmax * 0.28f), 0.0f, 1.0f);
      a = -d.accel * 0.6f * -in.throttle * (1.0f - ratio);
    }
  }
  float drag = 0.012f * vF * std::fabs(vF) + 0.35f * (vF > 0 ? 1.0f : -1.0f);
  if (std::fabs(vF) < 0.4f && std::fabs(in.throttle) < 0.02f) { vF *= (1.0f - 6.0f * dt); drag = 0; }
  else if (in.throttle <= 0.02f && in.throttle >= -0.02f) drag += vF * 0.18f;   // engine braking
  if (in.handbrake) { float hb = 8.5f; a -= (vF > 0 ? 1.0f : -1.0f) * std::min(hb, std::fabs(vF) / std::max(dt, 1e-3f) * 0.5f); }
  vF += (a - drag) * dt;

  // ---- steering (bicycle model)
  float speedFactor = clamp(std::fabs(vF) / (vmax * 0.65f), 0.0f, 1.0f);
  float maxSteer = d.steerMax * (1.0f - 0.78f * speedFactor);
  float targetSteer = in.steer * maxSteer;
  v.steerAngle += (targetSteer - v.steerAngle) * expDecay(9.0f, dt);
  float targetYawRate = vF / d.wheelbase * std::tan(v.steerAngle);
  float yawResp = in.handbrake ? 3.0f : 10.0f;
  v.yawRate += (targetYawRate - v.yawRate) * expDecay(yawResp, dt);
  if (in.handbrake && std::fabs(vF) > 6.0f) v.yawRate *= 1.0f + 0.5f * dt;
  v.yaw = wrapAngle(v.yaw + v.yawRate * dt);

  // ---- lateral grip: world velocity is re-expressed in the new heading, the sideways part decays
  Vec2 nf = fwd2(v.yaw), nr = right2(v.yaw);
  Vec2 world = fwd * vF + right * vL;
  float newLong = world.dot(nf), newLat = world.dot(nr);
  float grip = d.grip * (in.handbrake ? 0.22f : 1.0f);
  newLat *= std::exp(-grip * dt);
  v.vel = nf * newLong + nr * newLat;
  v.speed = v.vel.dot(nf);
  v.pos += v.vel * dt;

  // ---- collisions with the static world
  float impact = 0;
  phys::OBB me = vehicleObb(v);
  static thread_local std::vector<int> ids;
  float radius = std::max(d.length, d.width) * 0.6f + 0.3f;
  w.queryColliders(v.pos.x - radius, v.pos.y - radius, v.pos.x + radius, v.pos.y + radius, ids);
  for (int iter = 0; iter < 2; ++iter) {
    bool any = false;
    for (int id : ids) {
      const Collider& c = w.colliders[id];
      if (c.box.mn.y > 2.2f) continue;
      phys::Hit h = phys::obbVsAabb(me, c.box);
      if (!h.hit) continue;
      any = true;
      v.pos += h.normal * h.depth;
      me.c = v.pos;
      float vn = v.vel.dot(h.normal);
      if (vn < 0) {
        impact = std::max(impact, -vn);
        v.vel -= h.normal * (vn * 1.18f);   // remove the normal component with a small bounce
        v.yawRate *= 0.7f;
      }
    }
    if (!any) break;
  }
  // collisions with other vehicles
  for (const Vehicle& o : others) {
    if (o.id == v.id) continue;
    phys::Hit h = phys::obbVsObb(me, vehicleObb(o));
    if (!h.hit) continue;
    v.pos += h.normal * (h.depth * 0.75f);
    me.c = v.pos;
    float vn = (v.vel - o.vel).dot(h.normal);
    if (vn < 0) {
      impact = std::max(impact, -vn);
      v.vel -= h.normal * (vn * 1.2f);
    }
  }
  v.speed = v.vel.dot(fwd2(v.yaw));
  // world bounds
  float lim = World::kHalf - 2.5f;
  if (std::fabs(v.pos.x) < 200.0f) {
    v.pos.x = clamp(v.pos.x, -lim, lim);
    v.pos.y = clamp(v.pos.y, -lim, lim);
  }

  // ---- damage
  v.lastImpact = impact;
  if (impact > 3.5f) v.health = std::max(0.0f, v.health - (impact - 3.5f) * 2.4f);
  v.smokeTimer = v.health < 30.0f ? v.smokeTimer + dt : 0.0f;

  // ---- fuel
  if (v.engineOn && v.fuel > 0.0f) {
    float kmps = std::fabs(v.speed) / 1000.0f;
    float load = 0.65f + 0.7f * std::max(0.0f, in.throttle) + 0.2f * std::max(0.0f, -in.throttle);
    float perSec = d.consumption / 100.0f * kmps * load + 0.9f / 3600.0f;  // + idle burn
    v.fuel = std::max(0.0f, v.fuel - perSec * kFuelGameScale * dt);
  }

  // visual body motion
  float accelLong = (v.speed - (v.speed - a * dt));
  (void)accelLong;
  float targetPitch = clamp(-a * 0.004f, -0.03f, 0.03f);
  float targetRoll = clamp(v.yawRate * v.speed * 0.0022f, -0.05f, 0.05f);
  v.visualPitch += (targetPitch - v.visualPitch) * expDecay(6.0f, dt);
  v.visualRoll += (targetRoll - v.visualRoll) * expDecay(6.0f, dt);
  v.wheelSpin += v.speed * dt;
  return impact;
}

}  // namespace gtabr
