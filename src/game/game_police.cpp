// Wanted level (3 stars) and police: patrol, dispatch, investigation, pursuit (on foot and by car), search, arrest.
// The police never read the player's position directly: officers only know what they see (distance, field of view,
// line of sight), what witnesses report (the crime location) and what they hear (gunshots).
#include <algorithm>
#include <cmath>

#include "../core/log.h"
#include "game.h"

namespace gtabr {

namespace {
const float kRoad[3] = {-48.0f, 0.0f, 48.0f};
const float kEdge = 78.0f;
// thresholds of accumulated crime weight for 1, 2 and 3 stars
const float kHeat[3] = {0.5f, 3.0f, 6.0f};
float searchTimeFor(int level) { return 22.0f + 9.0f * level; }
int unitsFor(int level) { return level <= 0 ? 0 : (level == 1 ? 1 : (level == 2 ? 2 : 3)); }
}  // namespace

// ------------------------------------------------------------------------------------------------ roads
Vec2 Game::roadPointNear(Vec2 p) const {
  // closest point on the street grid (centre lines)
  Vec2 best = p;
  float bd = 1e9f;
  for (float r : kRoad) {
    Vec2 a{clamp(p.x, -kEdge, kEdge), r};
    Vec2 b{r, clamp(p.y, -kEdge, kEdge)};
    float da = (a - p).length(), db = (b - p).length();
    if (da < bd) { bd = da; best = a; }
    if (db < bd) { bd = db; best = b; }
  }
  return best;
}

// Steers an AI car along the street grid toward a target (Manhattan route through intersections), with a
// three-point turn when stuck. Collisions/physics are the same as for every other car.
void Game::driveAi(Vehicle& v, Vec2 target, float maxSpeed, float dt) {
  Vec2 here = v.pos;
  Vec2 goal = target;
  float distGoal = (goal - here).length();
  if (distGoal > 14.0f) {
    // route: leave the current street at the intersection that lines up with the target's street
    Vec2 a = roadPointNear(here), b = roadPointNear(goal);
    bool aHoriz = std::fabs(a.y - std::round(a.y / 48.0f) * 48.0f) < 0.5f && std::fabs(a.y) <= 48.5f &&
                  (std::fabs(a.y + 48) < 0.5f || std::fabs(a.y) < 0.5f || std::fabs(a.y - 48) < 0.5f);
    Vec2 next = b;
    if (std::fabs(a.x - b.x) > 1.0f && std::fabs(a.y - b.y) > 1.0f) {
      // pick the corner: travel along our street to the cross street nearest the target
      if (aHoriz) {
        float cx = kRoad[0];
        for (float r : kRoad) if (std::fabs(r - b.x) < std::fabs(cx - b.x)) cx = r;
        next = {cx, a.y};
      } else {
        float cz = kRoad[0];
        for (float r : kRoad) if (std::fabs(r - b.y) < std::fabs(cz - b.y)) cz = r;
        next = {a.x, cz};
      }
      if ((next - here).length() < 6.0f) next = b;
    }
    goal = next;
  }
  Vec2 d = goal - here;
  float dist = d.length();
  VehicleInput in;
  if (v.aiReverse > 0) {
    v.aiReverse -= dt;
    float err = wrapAngle(yawFromDir(d) - v.yaw);
    in.throttle = -0.8f;
    in.steer = err > 0 ? -1.0f : 1.0f;
  } else {
    float err = wrapAngle(yawFromDir(d) - v.yaw);
    in.steer = clamp(err * 2.0f, -1.0f, 1.0f);
    float tgt = clamp(dist * 0.6f, 3.0f, maxSpeed) * clamp(1.0f - std::fabs(err) / 1.4f, 0.25f, 1.0f);
    if (distGoal < 6.0f) tgt = 0;
    in.throttle = clamp((tgt - v.speed) * 0.4f, -1.0f, 1.0f);
    in.handbrake = distGoal < 4.0f && std::fabs(v.speed) < 1.0f;
    if (in.throttle > 0.3f && std::fabs(v.speed) < 0.5f) v.aiStuck += dt; else v.aiStuck = std::max(0.0f, v.aiStuck - dt);
    if (v.aiStuck > 1.4f) { v.aiReverse = 1.1f; v.aiStuck = 0; }
  }
  v.engineOn = true;
  float impact = stepVehicle(v, in, dt, world_, vehicles_);
  if (impact > 6.0f) audio_.play("crash", {v.pos.x, 0.8f, v.pos.y}, 0.6f);
  // run-over check for AI cars (pedestrians and the player)
  if (std::fabs(v.speed) > 2.0f) {
    phys::OBB o = vehicleObb(v);
    if (player_.vehicle < 0 && !player_.dead) {
      phys::Hit h = phys::circleVsObb(player_.pos, 0.32f, o);
      if (h.hit) player_.pos += h.normal * h.depth;
    }
  }
}

// ------------------------------------------------------------------------------------------------ perception
bool Game::copCanSee(const Npc& c, Vec2 target) const {
  if (player_.indoors != c.interior) return false;
  Vec2 d = target - c.pos;
  float dist = d.length();
  float range = day_.night > 0.5f ? 24.0f : 34.0f;
  if (player_.vehicle >= 0) range += 8.0f;   // a car is easier to spot
  if (dist > range) return false;
  if (dist > 5.0f) {
    float c0 = d.dot(fwd2(c.yaw)) / std::max(dist, 1e-3f);
    if (c0 < std::cos(70.0f * kDeg2Rad)) return false;   // 140 degree field of view
  }
  return lineOfSight(c.pos, target, 1.5f);
}

// ------------------------------------------------------------------------------------------------ wanted level
void Game::reportCrime(Vec2 where, float severity, bool witnessedByCop) {
  if (player_.dead) return;
  wantedHeat_ += severity;
  int lvl = 0;
  for (int i = 0; i < 3; ++i) if (wantedHeat_ >= kHeat[i]) lvl = i + 1;
  if (lvl > wanted_) {
    wanted_ = lvl;
    toast(wanted_ == 1 ? "Polícia acionada: procurado" : (wanted_ == 2 ? "Procurado: nível 2" : "Procurado: nível 3 - resposta armada"),
          "pin", rgba(1.0f, 0.45f, 0.4f));
  }
  if (wanted_ == 0) return;
  // the police only learn WHERE it happened; officers must find the player themselves
  if (witnessedByCop) { wantedLastKnown_ = player_.pos; sinceSeen_ = 0; }
  else if (sinceSeen_ > 3.0f) wantedLastKnown_ = where;
  evadeT_ = 0;
  // every officer not already chasing heads to the reported location
  for (Npc& c : npcs_) {
    if (!c.police || c.despawn || c.state == NpcState::Dead || c.state == NpcState::Down) continue;
    if (c.state == NpcState::CopChase || c.state == NpcState::Fight) continue;
    c.state = NpcState::CopInvestigate;
    c.lastKnown = wantedLastKnown_;
    c.path.clear();
  }
}

void Game::updateWanted(float dt) {
  // sight: does any officer see the player right now?
  bool seen = false;
  if (wanted_ > 0 && !player_.dead) {
    Vec2 pp = player_.pos;
    for (const Npc& c : npcs_)
      if (c.police && !c.despawn && c.state != NpcState::Dead && c.state != NpcState::Down && copCanSee(c, pp)) { seen = true; break; }
    for (const Vehicle& v : vehicles_)
      if (v.police && !v.despawn && v.driver >= 0 && !v.wrecked) {
        Vec2 d = pp - v.pos;
        if (d.length() < 30.0f && lineOfSight(v.pos, pp, 1.4f)) { seen = true; break; }
      }
  }
  if (seen) { sinceSeen_ = 0; wantedLastKnown_ = player_.pos; evadeT_ = 0; }
  else sinceSeen_ += dt;
  // evasion: after losing contact the police search for a while, then stand down
  if (wanted_ > 0 && !seen) {
    evadeT_ += dt;
    if (evadeT_ > searchTimeFor(wanted_)) {
      wanted_ = 0;
      wantedHeat_ = 0;
      toast("Você despistou a polícia", "pin", rgba(0.6f, 1.0f, 0.7f));
      for (Npc& c : npcs_) if (c.police && !c.despawn && c.state != NpcState::Dead) { c.state = NpcState::CopReturn; c.path.clear(); }
    }
  }
  // heat cools slowly while not wanted (petty crimes are forgotten)
  if (wanted_ == 0) wantedHeat_ = std::max(0.0f, wantedHeat_ - dt * 0.02f);
  // arrest: an officer next to a stopped (or downed) player for a moment
  if (wanted_ > 0 && wanted_ < 3 && !player_.dead && player_.vehicle < 0) {
    static float arrestT = 0;
    bool cuffing = false;
    for (const Npc& c : npcs_)
      if (c.police && !c.despawn && (c.state == NpcState::Fight || c.state == NpcState::CopChase) && (c.pos - player_.pos).length() < 1.4f &&
          (player_.speed < 0.8f || player_.down))
        cuffing = true;
    arrestT = cuffing ? arrestT + dt : 0.0f;
    if (arrestT > 1.6f) {
      arrestT = 0;
      int fine = std::min(moneyCents_, 5000 + 5000 * wanted_);
      moneyCents_ -= fine;
      wanted_ = 0; wantedHeat_ = 0;
      fadeTarget_ = 1.0f;
      fadeThen_ = [this, fine]() {
        teleportPlayer({40.0f, -58.0f}, 0);
        player_.down = false; player_.hitStun = 0; player_.animReq = -1; playerAnim_ = CharAnim{};
        for (Npc& c : npcs_) if (c.police) c.despawn = true;
        for (Vehicle& v : vehicles_) if (v.police && v.occupant < 0) { v.despawn = true; v.pos = {9999, 9999}; }
        fadeTarget_ = 0;
        toast("Preso! Fiança paga: " + fmtMoney(fine), "pin", rgba(1.0f, 0.6f, 0.4f));
      };
    }
  }
}

// ------------------------------------------------------------------------------------------------ spawning
void Game::spawnPoliceUnit(Vec2 dest, bool onFoot) {
  // a spawn spot on the streets, 55-90 m from the destination and not right in front of the player
  Vec2 spawn = roadPointNear(dest);
  float bestScore = -1e9f;
  for (int t = 0; t < 24; ++t) {
    Vec2 c = roadPointNear(dest + fwd2(rng_.range(0.0f, kTau)) * rng_.range(55.0f, 90.0f));
    float dp = (c - player_.pos).length();
    float score = (dp > 35.0f ? 10.0f : 0.0f) - std::fabs((c - dest).length() - 65.0f) * 0.1f;
    if (score > bestScore) { bestScore = score; spawn = c; }
  }
  // vehicle (pooled)
  int vi = -1;
  for (size_t i = 0; i < vehicles_.size(); ++i) if (vehicles_[i].despawn && vehicles_[i].police) { vi = (int)i; break; }
  if (vi < 0) { vi = (int)vehicles_.size(); vehicles_.push_back(Vehicle()); }
  Vehicle& v = vehicles_[vi];
  v = Vehicle();
  v.id = vi;
  v.model = kPoliceCarModel;
  v.police = true;
  v.siren = !onFoot;
  v.pos = spawn;
  v.yaw = yawFromDir(dest - spawn);
  v.fuel = 40;
  v.health = 100;
  v.engineOn = true;
  v.driver = 0;          // AI driven (crew spawn on arrival)
  v.aiTarget = dest;
  (void)onFoot;
}

// ------------------------------------------------------------------------------------------------ police update
void Game::updatePolice(float dt) {
  if (player_.indoors) return;
  // patrol: one quiet unit cruising the grid while nobody is wanted
  int activeCars = 0, patrolCars = 0;
  for (const Vehicle& v : vehicles_) if (v.police && !v.despawn) { ++activeCars; if (!v.siren) ++patrolCars; }
  policeSpawnT_ -= dt;
  if (wanted_ == 0 && patrolCars == 0 && policeSpawnT_ <= 0 && time_ > 20.0f) {
    spawnPoliceUnit(roadPointNear(player_.pos + fwd2(rng_.range(0.0f, kTau)) * 40.0f), true);
    policeSpawnT_ = 30.0f;
  }
  // dispatch: units according to the wanted level, sent to the last known position (never to the player directly)
  int responding = 0;
  for (const Vehicle& v : vehicles_) if (v.police && !v.despawn && v.siren) ++responding;
  for (const Npc& c : npcs_) if (c.police && !c.despawn && c.state != NpcState::Dead && c.unit < 0) ++responding;
  if (wanted_ > 0 && policeSpawnT_ <= 0) {
    int want = unitsFor(wanted_);
    int carsResponding = 0;
    for (const Vehicle& v : vehicles_) if (v.police && !v.despawn && v.siren) ++carsResponding;
    if (carsResponding < want) {
      // the patrol car (if any) joins first
      bool joined = false;
      for (Vehicle& v : vehicles_)
        if (v.police && !v.despawn && !v.siren && v.driver >= 0) { v.siren = true; v.aiTarget = wantedLastKnown_; joined = true; break; }
      if (!joined) spawnPoliceUnit(wantedLastKnown_, false);
      policeSpawnT_ = wanted_ >= 3 ? 6.0f : 10.0f;
    }
  }
  (void)responding;
  (void)activeCars;

  // police cars
  bool anySiren = false;
  Vec3 sirenPos{};
  float sirenDist = 1e9f;
  for (Vehicle& v : vehicles_) {
    if (!v.police || v.despawn) continue;
    if (v.occupant == 0) { v.siren = false; v.driver = -1; continue; }   // stolen by the player
    if (v.driver < 0) continue;                                           // parked, crew on foot
    if (v.health <= 0) { v.wrecked = true; v.driver = -1; v.siren = false; continue; }
    float dp = (v.pos - player_.pos).length();
    if (v.siren) {
      // pursuit by car when the suspect is driving and visible, otherwise go to the last known position
      bool chaseCar = player_.vehicle >= 0 && sinceSeen_ < 4.0f;
      v.aiTarget = chaseCar ? vehicles_[player_.vehicle].pos : wantedLastKnown_;
      float spd = chaseCar ? 24.0f : 16.0f;
      driveAi(v, v.aiTarget, spd, dt);
      float dt2 = (v.pos - v.aiTarget).length();
      // arrived (or the suspect is on foot nearby): stop and deploy the crew
      bool deploy = (!chaseCar && dt2 < 9.0f) || (player_.vehicle < 0 && dp < 14.0f && sinceSeen_ < 2.0f) || (v.aiStuck > 1.0f && dt2 < 20.0f);
      if (wanted_ == 0) deploy = false;
      if (deploy && std::fabs(v.speed) < 3.0f) {
        v.driver = -1;
        v.engineOn = false;
        int crew = wanted_ >= 3 ? 2 : 2;
        for (int k = 0; k < crew; ++k) {
          int slot = -1;
          for (size_t i = 0; i < npcs_.size(); ++i) if (npcs_[i].police && npcs_[i].despawn) { slot = (int)i; break; }
          if (slot < 0) { slot = (int)npcs_.size(); npcs_.push_back(Npc()); }
          Npc c;
          c.id = slot;
          c.archetype = "policial";
          c.role = 5;
          c.police = true;
          c.unit = v.id;
          c.pos = v.pos + right2(v.yaw) * (k ? 1.4f : -1.4f);
          phys::depenetrateCircle(world_, c.pos, 0.3f);
          c.y = world_.heightAt(c.pos.x, c.pos.y);
          c.yaw = v.yaw;
          c.bravery = 1.0f;
          c.weapon = wanted_ >= 2 || isFirearm(player_.weapon) ? kWpnPistol : kWpnBaton;
          c.state = sinceSeen_ < 2.0f ? NpcState::CopChase : NpcState::CopInvestigate;
          c.lastKnown = wantedLastKnown_;
          c.health = 100;
          c.walkSpeed = 1.4f;
          npcs_[slot] = c;
        }
      }
    } else {
      // quiet patrol around the block grid; the patrol is a witness on wheels
      if ((v.pos - v.aiTarget).length() < 10.0f) v.aiTarget = roadPointNear(v.pos + fwd2(rng_.range(0.0f, kTau)) * 60.0f);
      driveAi(v, v.aiTarget, 9.0f, dt);
      if (dp > 140.0f) { v.despawn = true; v.pos = {9999, 9999}; }
    }
    if (v.siren && dp < sirenDist) { sirenDist = dp; sirenPos = {v.pos.x, 1.2f, v.pos.y}; anySiren = true; }
  }
  // patrol witnesses: crimes in sight of a patrolling car
  for (const WorldEvent& e : events_) {
    if (e.instigator.kind != ActorKind::Player || e.severity <= 0) continue;
    for (Vehicle& v : vehicles_)
      if (v.police && !v.despawn && !v.siren && v.driver >= 0 && (v.pos - e.pos).length() < 28.0f && lineOfSight(v.pos, e.pos)) {
        v.siren = true;
        reportCrime(e.pos, e.severity * 0.5f, lineOfSight(v.pos, player_.pos));
        break;
      }
  }
  // the parked cars of officers that left are tidied up once nobody needs them
  for (Vehicle& v : vehicles_) {
    if (!v.police || v.despawn || v.driver >= 0 || v.occupant >= 0) continue;
    bool crewLeft = false;
    for (const Npc& c : npcs_) if (c.police && !c.despawn && c.unit == v.id) crewLeft = true;
    if (!crewLeft && (v.pos - player_.pos).length() > 70.0f) { v.despawn = true; v.pos = {9999, 9999}; }
  }
  // siren audio: one loop following the nearest responding car
  if (anySiren) {
    if (!sirenHandle_) sirenHandle_ = audio_.loopStart("siren", sirenPos, 0.5f);
    audio_.loopUpdate(sirenHandle_, sirenPos, 0.55f);
  } else if (sirenHandle_) { audio_.loopStop(sirenHandle_); sirenHandle_ = 0; }
}

// ------------------------------------------------------------------------------------------------ officers
void Game::copThink(Npc& c, float dt) {
  if (c.state == NpcState::Dead) {
    c.deadT += dt;
    if (c.deadT > 45.0f && (c.pos - player_.pos).length() > 50.0f) c.despawn = true;
    return;
  }
  if (c.knockVel.lengthSq() > 1e-4f) {
    phys::moveCircle(world_, c.pos, c.knockVel * dt, 0.3f);
    c.knockVel = c.knockVel * (1.0f - expDecay(7.0f, dt));
  }
  if (c.state == NpcState::Down) {
    c.downT -= dt;
    if (c.downT <= 0) { requestAnim(c, kActStandUp, 3.2f); c.state = NpcState::CopChase; c.hitStun = 1.2f; }
    return;
  }
  if (c.hitStun > 0) { c.hitStun -= dt; c.speed = 0; return; }
  c.stateTimer -= dt;
  const Vec2 pp = player_.pos;
  const bool sees = wanted_ > 0 && !player_.dead && copCanSee(c, pp);
  if (sees) { c.lastKnown = pp; c.lastSeenT = 0; }
  else c.lastSeenT += dt;
  // hearing: gunshots by anyone draw officers to the sound (the position of the shot, not the shooter)
  for (const WorldEvent& e : events_) {
    if (e.time <= c.lastEventT) continue;
    if (e.instigator.kind == ActorKind::Npc && npcs_[e.instigator.index].police) continue;
    float d = (e.pos - c.pos).length();
    if (d > e.radius) continue;
    bool see = d < 4.0f || lineOfSight(c.pos, e.pos);
    if (e.instigator.kind == ActorKind::Player && e.severity > 0) {
      // a cop who sees the crime makes the player wanted (and knows exactly who)
      if (see) reportCrime(e.pos, e.severity, true);
      else if (c.state == NpcState::CopPatrol || c.state == NpcState::CopSearch || c.state == NpcState::CopInvestigate) {
        c.state = NpcState::CopInvestigate;
        c.lastKnown = e.pos;
        c.path.clear();
      }
    }
  }
  if (!events_.empty()) c.lastEventT = std::max(c.lastEventT, events_.back().time);
  if (wanted_ == 0 && c.state != NpcState::CopReturn) { c.state = NpcState::CopReturn; c.path.clear(); }
  if (sees && c.state != NpcState::Fight) { c.state = NpcState::CopChase; }

  auto moveTo = [&](Vec2 target, float speed) {
    // short hops: straight line with sliding; longer trips use the navmesh
    Vec2 to = target - c.pos;
    float dist = to.length();
    if (dist < 0.3f) { c.speed = 0; return true; }
    Vec2 step;
    if (dist > 6.0f && !lineOfSight(c.pos, target, 0.8f)) {
      if (c.path.empty() || c.pathIdx >= c.path.size() || c.repath <= 0) {
        c.path.clear();
        navOutdoor_.findPath(c.pos, target, c.path);
        c.pathIdx = 0;
        c.repath = 1.5f;
      }
      if (c.pathIdx < c.path.size()) {
        Vec2 wp = c.path[c.pathIdx];
        if ((wp - c.pos).length() < 0.6f) ++c.pathIdx;
        to = wp - c.pos;
        dist = std::max(to.length(), 1e-3f);
      }
    }
    step = to / std::max(dist, 1e-3f) * speed * dt;
    Vec2 before = c.pos;
    phys::moveCircle(world_, c.pos, step, 0.3f);
    c.speed = (c.pos - before).length() / std::max(dt, 1e-4f);
    if (c.speed > 0.2f) c.yaw = lerpAngle(c.yaw, yawFromDir(to), expDecay(9.0f, dt));
    return false;
  };

  switch (c.state) {
    case NpcState::CopInvestigate: {
      // go to the reported / last known spot and look around
      bool there = moveTo(c.lastKnown, 4.2f);
      if (there || (c.lastKnown - c.pos).length() < 2.5f) {
        c.state = NpcState::CopSearch;
        c.searchT = 0;
        c.searchPoint = c.lastKnown;
      }
      break;
    }
    case NpcState::CopChase: {
      if (player_.dead || wanted_ == 0) { c.state = NpcState::CopReturn; break; }
      float d = (pp - c.pos).length();
      if (!sees) {
        // lost sight: run to the last known position, then search the area
        bool there = moveTo(c.lastKnown, 5.6f);
        if (there || c.lastSeenT > 6.0f) { c.state = NpcState::CopSearch; c.searchT = 0; c.searchPoint = c.lastKnown; }
        break;
      }
      if (player_.vehicle >= 0) { moveTo(pp, 5.6f); break; }
      // armed response: shoot at level 3 or when the suspect has a firearm out / is shooting
      bool shoot = isFirearm(c.weapon) && (wanted_ >= 3 || (isFirearm(player_.weapon) && player_.aimHold > 0) || wanted_ >= 2 && d > 8.0f);
      if (shoot && d < 24.0f) {
        c.speed = 0;
        c.yaw = lerpAngle(c.yaw, yawFromDir(pp - c.pos), expDecay(10.0f, dt));
        c.attackCd -= dt;
        if (c.attackCd <= 0 && std::fabs(wrapAngle(yawFromDir(pp - c.pos) - c.yaw)) < 0.25f) {
          Vec2 dir = (pp - c.pos).normalized();
          // officers are not perfect shots: aim error grows with distance and the target's speed
          float err = (0.05f + d * 0.004f + player_.speed * 0.02f) * (rng_.uni() * 2.0f - 1.0f);
          dir = fwd2(yawFromDir(dir) + err);
          fireWeapon({ActorKind::Npc, c.id}, {c.pos.x + dir.x * 0.5f, c.y + 1.35f, c.pos.y + dir.y * 0.5f}, dir, c.weapon);
          c.attackCd = rng_.range(0.55f, 1.1f);
          npcAnim_.size() > (size_t)c.id ? void(npcAnim_[c.id].recoil = 0.7f) : void();
        }
        if (d > 14.0f) moveTo(pp, 3.0f);
        break;
      }
      // close in to fight / arrest
      if (d > 1.3f) moveTo(pp, 5.4f);
      else { c.state = NpcState::Fight; c.target = {ActorKind::Player, 0}; c.stateTimer = 30.0f; c.weapon = c.weapon == kWpnPistol && wanted_ < 3 ? kWpnBaton : c.weapon; }
      if (c.greetCooldown <= 0) {
        static const char* shout[] = {"Parado! Polícia!", "Mãos na cabeça!", "Para aí!", "Encosta na parede!", "Perdeu, perdeu!"};
        npcSay(c, shout[rng_.irange(0, 4)], 1.8f);
        c.greetCooldown = 4.0f;
      }
      break;
    }
    case NpcState::Fight: {
      if (!sees && c.lastSeenT > 1.5f) { c.state = NpcState::CopChase; break; }
      if ((pp - c.pos).length() > 2.2f) { c.state = NpcState::CopChase; break; }
      npcFight(c, dt);
      if (c.state == NpcState::Idle) c.state = NpcState::CopChase;
      break;
    }
    case NpcState::CopSearch: {
      // search pattern around the last known position, widening over time
      c.searchT += dt;
      float radius = std::min(6.0f + c.searchT * 1.2f, 40.0f);
      if ((c.searchPoint - c.pos).length() < 1.5f || c.stateTimer <= 0) {
        Vec2 p;
        if (!navOutdoor_.randomPoint(rng_, p)) p = c.lastKnown;
        for (int t = 0; t < 8 && (p - c.lastKnown).length() > radius; ++t) navOutdoor_.randomPoint(rng_, p);
        if ((p - c.lastKnown).length() > radius) p = c.lastKnown + fwd2(rng_.range(0.0f, kTau)) * radius * 0.5f;
        c.searchPoint = p;
        c.path.clear();
        c.stateTimer = 8.0f;
      }
      moveTo(c.searchPoint, 2.6f);
      break;
    }
    case NpcState::CopReturn: {
      // walk back to the car and leave; officers far from the player simply disappear
      Vec2 carPos = c.unit >= 0 && c.unit < (int)vehicles_.size() && !vehicles_[c.unit].despawn ? vehicles_[c.unit].pos : c.pos;
      bool there = moveTo(carPos, 2.4f) || (carPos - c.pos).length() < 2.2f;
      float dp = (c.pos - pp).length();
      if (there || dp > 60.0f) {
        c.despawn = true;
        c.pos = {9999, 9999};
        if (c.unit >= 0 && c.unit < (int)vehicles_.size()) {
          Vehicle& v = vehicles_[c.unit];
          bool others = false;
          for (const Npc& o : npcs_) if (&o != &c && o.police && !o.despawn && o.unit == c.unit) others = true;
          if (!others && v.occupant < 0) { v.driver = 0; v.siren = false; v.aiTarget = roadPointNear(v.pos + fwd2(v.yaw) * 120.0f); }
        }
      }
      break;
    }
    default: c.state = NpcState::CopInvestigate; break;
  }
  c.greetCooldown = std::max(0.0f, c.greetCooldown - dt);
  c.y += (world_.heightAt(c.pos.x, c.pos.y) - c.y) * expDecay(15.0f, dt);
}

}  // namespace gtabr
