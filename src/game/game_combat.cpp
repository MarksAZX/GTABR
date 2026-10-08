// Combat: player attacks (combo punches/kick, melee weapons, firearms with auto-aim), real hit detection
// (melee arc vs actors/vehicles, raycasts vs world/actors/vehicles), the central damage system, world events,
// pickups and combat effects (tracers, muzzle flashes, blood, impacts).
#include <algorithm>
#include <cmath>

#include "../core/log.h"
#include "game.h"

namespace gtabr {

namespace {
// ray (2D) vs circle; returns distance along the ray or -1
float rayCircle(Vec2 o, Vec2 d, Vec2 c, float r) {
  Vec2 m = o - c;
  float b = m.dot(d), cc = m.dot(m) - r * r;
  if (cc > 0 && b > 0) return -1;
  float disc = b * b - cc;
  if (disc < 0) return -1;
  float t = -b - std::sqrt(disc);
  return t < 0 ? 0 : t;
}
// ray (2D) vs oriented box (vehicles)
float rayObb(Vec2 o, Vec2 d, const phys::OBB& b) {
  Vec2 ax = right2(b.yaw), az = fwd2(b.yaw);
  Vec2 lo{(o - b.c).dot(ax), (o - b.c).dot(az)};
  Vec2 ld{d.dot(ax), d.dot(az)};
  float tmin = 0, tmax = 1e9f;
  for (int k = 0; k < 2; ++k) {
    float oo = k ? lo.y : lo.x, dd = k ? ld.y : ld.x, h = k ? b.half.y : b.half.x;
    if (std::fabs(dd) < 1e-6f) { if (std::fabs(oo) > h) return -1; continue; }
    float t1 = (-h - oo) / dd, t2 = (h - oo) / dd;
    if (t1 > t2) std::swap(t1, t2);
    tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
    if (tmin > tmax) return -1;
  }
  return tmin;
}
}  // namespace

// ------------------------------------------------------------------------------------------------ helpers
Vec2 Game::actorPos(ActorRef r) const {
  switch (r.kind) {
    case ActorKind::Player: return player_.pos;
    case ActorKind::Npc: return r.index >= 0 && r.index < (int)npcs_.size() ? npcs_[r.index].pos : Vec2{};
    case ActorKind::Vehicle: return r.index >= 0 && r.index < (int)vehicles_.size() ? vehicles_[r.index].pos : Vec2{};
    default: return {};
  }
}

bool Game::actorAlive(ActorRef r) const {
  switch (r.kind) {
    case ActorKind::Player: return !player_.dead;
    case ActorKind::Npc: return r.index >= 0 && r.index < (int)npcs_.size() && npcs_[r.index].state != NpcState::Dead && !npcs_[r.index].despawn;
    case ActorKind::Vehicle: return r.index >= 0 && r.index < (int)vehicles_.size() && !vehicles_[r.index].wrecked;
    default: return false;
  }
}

bool Game::lineOfSight(Vec2 a, Vec2 b, float height) const {
  Vec2 d = b - a;
  float len = d.length();
  if (len < 1e-3f) return true;
  return phys::raycast(world_, a, d / len, len, height) >= len - 0.05f;
}

void Game::emitEvent(EventKind k, Vec2 pos, float radius, ActorRef who, ActorRef victim, float severity) {
  events_.push_back({k, pos, radius, who, victim, time_, severity});
}

void Game::giveWeapon(int weapon, int ammo) {
  if (weapon <= 0 || weapon >= kWeaponCount) return;
  Player& p = player_;
  bool had = p.owned[weapon];
  p.owned[weapon] = true;
  const WeaponDef& d = weaponDef(weapon);
  if (d.magazine > 0) {
    int fill = std::min(ammo, d.magazine - p.mag[weapon]);
    if (!had) { p.mag[weapon] += fill; ammo -= fill; }
    p.reserve[weapon] = std::min(d.reserveMax, p.reserve[weapon] + ammo);
  }
}

void Game::equipWeapon(int weapon) {
  if (weapon < 0 || weapon >= kWeaponCount || !player_.owned[weapon]) return;
  if (player_.weapon == weapon) return;
  player_.weapon = weapon;
  player_.reloadT = -1;
  player_.attackT = -1;
  player_.combo = 0;
  audio_.play(isFirearm(weapon) ? "reload" : "swing", {player_.pos.x, 1.0f, player_.pos.y}, 0.5f);
  if (weapon > 0) emitEvent(EventKind::WeaponDrawn, player_.pos, isFirearm(weapon) ? 14.0f : 6.0f, {ActorKind::Player, 0}, {}, 0);
}

int Game::aliveCops() const {
  int n = 0;
  for (const Npc& c : npcs_) if (c.police && c.state != NpcState::Dead && !c.despawn) ++n;
  return n;
}
int Game::copsChasing() const {
  int n = 0;
  for (const Npc& c : npcs_) if (c.police && (c.state == NpcState::CopChase || c.state == NpcState::Fight)) ++n;
  return n;
}

// Best target in a cone: alive NPCs (hostile ones preferred), with line of sight.
int Game::findAimTarget(Vec2 from, Vec2 dir, float range, float cosCone, bool preferHostile) const {
  int best = -1;
  float bestScore = 1e9f;
  for (size_t i = 0; i < npcs_.size(); ++i) {
    const Npc& n = npcs_[i];
    if (n.interior != player_.indoors || n.state == NpcState::Dead || n.despawn) continue;
    Vec2 d = n.pos - from;
    float dist = d.length();
    if (dist > range || dist < 0.1f) continue;
    float c = d.dot(dir) / dist;
    if (c < cosCone) continue;
    if (!lineOfSight(from, n.pos)) continue;
    bool hostile = (n.state == NpcState::Fight && n.target.kind == ActorKind::Player) || (n.police && wanted_ > 0);
    float score = dist * (1.6f - c) * (preferHostile && hostile ? 0.4f : 1.0f) * (n.state == NpcState::Down ? 3.0f : 1.0f);
    if (score < bestScore) { bestScore = score; best = (int)i; }
  }
  return best;
}

// ------------------------------------------------------------------------------------------------ damage
void Game::applyDamage(ActorRef target, const DamageInfo& d) {
  const bool byPlayer = d.attacker.kind == ActorKind::Player;
  if (target.kind == ActorKind::Player) {
    Player& p = player_;
    if (p.dead) return;
    float mul = p.vehicle >= 0 && d.type == DamageType::Bullet ? 0.6f : 1.0f;   // the car body absorbs some rounds
    p.health -= d.amount * mul;
    p.hurtTimer = 0.6f;
    healthShow_ = 4.0f;
    cameraShake_ = std::max(cameraShake_, clamp(d.amount / 40.0f, 0.15f, 0.8f));
    audio_.play("hurt", {p.pos.x, 1.2f, p.pos.y}, 0.7f);
    if (d.type == DamageType::Bullet || d.type == DamageType::Blade) spawnBlood({p.pos.x, 1.2f, p.pos.y}, d.dir, 6);
    if (p.health <= 0) { killPlayer(d); return; }
    if (p.vehicle < 0) {
      p.knockVel += d.dir * d.knockback;
      bool heavy = d.knockback > 3.5f || d.type == DamageType::RunOver;
      if (heavy && !p.down) { p.down = true; p.downT = 2.2f; p.attackT = -1; requestAnim(p, kActKnockDown, 1.4f, false, true); }
      else if (!p.down) { p.hitStun = 0.35f; p.attackT = -1; requestAnim(p, kActHit, 1.6f, true); }
    }
    return;
  }
  if (target.kind == ActorKind::Vehicle) {
    if (target.index < 0 || target.index >= (int)vehicles_.size()) return;
    Vehicle& v = vehicles_[target.index];
    float scale = d.type == DamageType::Bullet ? 0.35f : (d.type == DamageType::Crash ? 1.0f : 0.15f);
    v.health = std::max(0.0f, v.health - d.amount * scale);
    if (byPlayer) emitEvent(EventKind::Assault, v.pos, d.type == DamageType::Bullet ? 0 : 8.0f, d.attacker, target, v.police ? 1.2f : 0.4f);
    return;
  }
  if (target.kind != ActorKind::Npc || target.index < 0 || target.index >= (int)npcs_.size()) return;
  Npc& n = npcs_[target.index];
  if (n.state == NpcState::Dead) return;
  bool wasDown = n.state == NpcState::Down;
  n.health -= d.amount;
  n.hitStun = 0.45f;
  n.knockVel += d.dir * d.knockback;
  n.threat = d.attacker;
  n.threatPos = actorPos(d.attacker);
  n.fear = std::min(1.0f, n.fear + 0.5f);
  n.path.clear();
  Vec3 p3{n.pos.x, n.y + 1.1f, n.pos.y};
  if (d.type == DamageType::Bullet || d.type == DamageType::Blade || d.type == DamageType::RunOver) spawnBlood(p3, d.dir, d.type == DamageType::Bullet ? 8 : 5);
  audio_.play("hurt", p3, 0.6f, n.archetype.find("mulher") != std::string::npos || n.archetype == "atendente" ? 1.35f : 1.0f);
  // crime bookkeeping: assault / murder
  const bool lethal = n.health <= 0;
  if (byPlayer || d.attacker.kind == ActorKind::Vehicle) {
    float sev = n.police ? 1.6f : 0.7f;
    if (d.type == DamageType::Bullet) sev += 0.5f;
    if (lethal) sev += 1.0f;
    emitEvent(lethal ? EventKind::Kill : (n.police ? EventKind::CopAssault : EventKind::Assault), n.pos, d.type == DamageType::Bullet ? 25.0f : 16.0f,
              d.attacker, target, sev);
  }
  if (lethal) {
    // blunt / unarmed blows knock people out; blades, bullets and cars kill
    bool knockout = (d.type == DamageType::Unarmed || d.type == DamageType::Blunt) && n.health > -25.0f && !n.police;
    n.speed = 0;
    n.attackT = -1;
    n.bubble.clear();
    if (knockout) {
      n.state = NpcState::Down;
      n.downT = 9.0f + (float)(n.id % 5);
      n.health = 1;
    } else {
      n.state = NpcState::Dead;
      n.deadT = 0;
      stains_.push_back({{n.pos.x, n.y + 0.02f, n.pos.y}, 0.9f, 60.0f});
    }
    if (!wasDown) requestAnim(n, kActKnockDown, 1.3f, false, true);
    audio_.play("body", p3, 0.8f);
    return;
  }
  if (wasDown) return;   // still on the floor
  bool heavy = d.knockback > 4.0f || d.type == DamageType::RunOver;
  if (heavy) {
    n.state = NpcState::Down;
    n.downT = d.type == DamageType::RunOver ? 4.0f : 2.4f;
    requestAnim(n, kActKnockDown, 1.4f, false, true);
  } else {
    requestAnim(n, kActHit, 1.6f, true);
    // fight back or run: decided by personality, what hit us and who did it
    bool armedAttacker = d.type == DamageType::Bullet || (byPlayer && player_.weapon != kWpnFists);
    bool fightBack = n.police || (!armedAttacker && n.bravery > 0.55f) || (n.state == NpcState::Fight);
    if (fightBack && n.state != NpcState::Work) {
      n.state = NpcState::Fight;
      n.target = d.attacker;
      n.stateTimer = 20.0f;
      if (!n.police) npcSay(n, npcLine(n, 3));
    } else if (!n.police) {
      npcStartFlee(n, actorPos(d.attacker), 10.0f);
      npcSay(n, npcLine(n, 2));
    }
  }
}

void Game::killPlayer(const DamageInfo& d) {
  Player& p = player_;
  p.health = 0;
  p.dead = true;
  p.down = true;
  p.downT = 0;
  deathT_ = 0;
  p.attackT = p.reloadT = -1;
  if (p.vehicle >= 0) { vehicles_[p.vehicle].occupant = -1; vehicles_[p.vehicle].engineOn = false; p.vehicle = -1; }
  requestAnim(p, kActKnockDown, 1.2f, false, true);
  toast(d.attacker.kind == ActorKind::Npc && npcs_[d.attacker.index].police ? "Você foi abatido pela polícia" : "Você desmaiou", "heart",
        rgba(1.0f, 0.4f, 0.35f));
}

void Game::respawnPlayer() {
  Player& p = player_;
  // hospital/praça: the plaza in the north-west block, a quarter of the money is gone and the heat is off
  int lost = moneyCents_ / 10;
  moneyCents_ -= lost;
  teleportPlayer({-22.0f, -24.0f}, 0);
  p.dead = false; p.down = false; p.health = 100; p.hitStun = 0; p.knockVel = {};
  p.attackT = p.reloadT = -1;
  p.animReq = -1;
  playerAnim_ = CharAnim{};
  wanted_ = 0; wantedHeat_ = 0; evadeT_ = 0;
  for (Npc& c : npcs_) if (c.police) c.despawn = true;
  fadeTarget_ = 0;
  toast("Você acordou na praça. Despesas: " + fmtMoney(lost), "heart");
}

// ------------------------------------------------------------------------------------------------ melee
void Game::startMelee(ActorRef who, int kind) {
  // kind: 1 punch, 2 kick, 3 weapon swing; durations follow the clips (played faster for responsiveness)
  int act = kind == 2 ? kActKick : (kind == 3 ? kActSlash : kActPunch);
  float speed = kind == 2 ? 1.5f : (kind == 3 ? 1.35f : 2.2f);
  float dur = animator_.actionDuration(act) / speed;
  dur = clamp(dur, 0.35f, 1.1f);
  if (who.kind == ActorKind::Player) {
    Player& p = player_;
    p.attackT = 0; p.attackDur = dur; p.attackKind = kind; p.attackHit = false;
    requestAnim(p, act, animator_.actionDuration(act) / dur, kind != 2);
    audio_.play("swing", {p.pos.x, 1.2f, p.pos.y}, 0.35f, kind == 3 ? 0.8f : 1.2f);
  } else if (who.kind == ActorKind::Npc) {
    Npc& n = npcs_[who.index];
    n.attackT = 0; n.attackDur = dur; n.attackKind = kind; n.attackHit = false;
    requestAnim(n, act, animator_.actionDuration(act) / dur, kind != 2);
  }
}

// Resolves the blow at its impact frame. Returns true if something was hit.
bool Game::meleeStrike(ActorRef attacker, Vec2 pos, float yaw, int weapon, int kind) {
  const WeaponDef& w = weaponDef(weapon);
  float range = w.range + (kind == 2 ? 0.25f : 0.0f);
  float dmg = w.damage * (kind == 2 ? 1.6f : 1.0f);
  float knock = w.knockback * (kind == 2 ? 2.2f : 1.0f);
  Vec2 fwd = fwd2(yaw);
  const float cosArc = std::cos(55.0f * kDeg2Rad);
  bool indoors = attacker.kind == ActorKind::Player ? player_.indoors : npcs_[attacker.index].interior;
  // best target inside the arc
  ActorRef best;
  float bestD = 1e9f;
  auto consider = [&](ActorRef r, Vec2 p, float radius) {
    Vec2 d = p - pos;
    float dist = d.length() - radius;
    if (dist > range) return;
    float c = d.length() > 1e-3f ? d.dot(fwd) / d.length() : 1.0f;
    if (c < cosArc) return;
    if (dist < bestD) { bestD = dist; best = r; }
  };
  for (size_t i = 0; i < npcs_.size(); ++i) {
    if (attacker.kind == ActorKind::Npc && attacker.index == (int)i) continue;
    const Npc& n = npcs_[i];
    if (n.interior != indoors || n.state == NpcState::Dead || n.despawn) continue;
    consider({ActorKind::Npc, (int)i}, n.pos, 0.3f);
  }
  if (attacker.kind != ActorKind::Player && !player_.dead && player_.vehicle < 0 && player_.indoors == indoors)
    consider({ActorKind::Player, 0}, player_.pos, 0.3f);
  if (!indoors)
    for (size_t i = 0; i < vehicles_.size(); ++i) {
      float d = phys::circleVsObb(pos + fwd * (range * 0.6f), range * 0.5f, vehicleObb(vehicles_[i])).hit ? 0.0f : 1e9f;
      if (d < bestD * 0.5f && !best.valid()) { best = {ActorKind::Vehicle, (int)i}; bestD = range; }
    }
  Vec3 at{pos.x + fwd.x * range * 0.7f, 1.2f, pos.y + fwd.y * range * 0.7f};
  if (!best.valid()) return false;
  DamageInfo d;
  d.type = w.dmgType;
  d.amount = dmg;
  d.attacker = attacker;
  d.dir = fwd;
  d.knockback = knock;
  d.weapon = weapon;
  d.point = at;
  applyDamage(best, d);
  const char* snd = w.sound;
  if (best.kind == ActorKind::Vehicle) snd = "metal";
  audio_.play(snd, at, 0.9f, 0.9f + 0.2f * (float)(rng_.uni()));
  if (attacker.kind == ActorKind::Player) cameraShake_ = std::max(cameraShake_, 0.18f);
  emitEvent(EventKind::Assault, pos, 12.0f, attacker, best, 0);
  return true;
}

// ------------------------------------------------------------------------------------------------ firearms
void Game::fireWeapon(ActorRef shooter, Vec3 origin, Vec2 dir, int weapon) {
  const WeaponDef& w = weaponDef(weapon);
  Vec2 o{origin.x, origin.z};
  bool indoors = shooter.kind == ActorKind::Player ? player_.indoors : npcs_[shooter.index].interior;
  for (int pellet = 0; pellet < w.pellets; ++pellet) {
    float a = yawFromDir(dir) + (rng_.uni() * 2.0f - 1.0f) * w.spread;
    Vec2 d = fwd2(a);
    float maxDist = w.range * 1.6f;
    float wall = phys::raycast(world_, o, d, maxDist, origin.y);
    ActorRef hit;
    float best = wall;
    for (size_t i = 0; i < npcs_.size(); ++i) {
      if (shooter.kind == ActorKind::Npc && shooter.index == (int)i) continue;
      const Npc& n = npcs_[i];
      if (n.interior != indoors || n.state == NpcState::Dead || n.despawn) continue;
      float r = n.state == NpcState::Down ? 0.5f : 0.34f;
      float t = rayCircle(o, d, n.pos, r);
      if (t >= 0 && t < best) { best = t; hit = {ActorKind::Npc, (int)i}; }
    }
    if (shooter.kind != ActorKind::Player && !player_.dead && player_.indoors == indoors) {
      float t = player_.vehicle >= 0 ? -1.0f : rayCircle(o, d, player_.pos, 0.34f);
      if (t >= 0 && t < best) { best = t; hit = {ActorKind::Player, 0}; }
    }
    if (!indoors)
      for (size_t i = 0; i < vehicles_.size(); ++i) {
        float t = rayObb(o, d, vehicleObb(vehicles_[i]));
        if (t >= 0 && t < best) {
          best = t;
          hit = {ActorKind::Vehicle, (int)i};
          // a driver behind the glass can still be hit
          if (vehicles_[i].occupant == 0 && shooter.kind != ActorKind::Player && rng_.chance(0.35f)) hit = {ActorKind::Player, 0};
        }
      }
    Vec2 end2 = o + d * best;
    Vec3 end{end2.x, origin.y - 0.1f, end2.y};
    tracers_.push_back({origin, end, 0.07f});
    if (hit.valid()) {
      // falloff beyond the effective range
      float fall = best > w.range ? clamp(1.0f - (best - w.range) / (w.range * 0.6f), 0.2f, 1.0f) : 1.0f;
      DamageInfo di;
      di.type = DamageType::Bullet;
      di.amount = w.damage * fall;
      di.attacker = shooter;
      di.dir = d;
      di.knockback = w.knockback;
      di.weapon = weapon;
      di.point = end;
      applyDamage(hit, di);
      if (hit.kind == ActorKind::Vehicle) { spawnImpact(end, 4); audio_.play("metal", end, 0.4f, 1.4f); }
    } else if (best < maxDist - 0.1f) spawnImpact(end, 5);
  }
  audio_.play(w.sound, origin, 1.0f, 0.95f + 0.1f * rng_.uni());
  muzzleT_ = 0.06f;
  muzzlePos_ = origin;
  emitEvent(EventKind::Gunshot, o, w.noise, shooter, {}, shooter.kind == ActorKind::Player ? 0.8f : 0.0f);
}

// ------------------------------------------------------------------------------------------------ player
void Game::updatePlayerAttack(float dt, const InputFrame& in) {
  Player& p = player_;
  p.fireCooldown = std::max(0.0f, p.fireCooldown - dt);
  p.comboWindow = std::max(0.0f, p.comboWindow - dt);
  p.aimHold = std::max(0.0f, p.aimHold - dt);
  p.hitStun = std::max(0.0f, p.hitStun - dt);
  if (p.dead || p.vehicle >= 0 || p.entering || p.exiting || p.down || p.swimming) { p.attackT = -1; p.reloadT = -1; p.aimHold = 0; return; }
  const WeaponDef& w = weaponDef(p.weapon);
  const bool firearm = w.magazine > 0;

  // ---- reload
  if (p.reloadT >= 0) {
    p.reloadT += dt;
    if (p.reloadT >= w.reloadTime) {
      int need = w.magazine - p.mag[p.weapon];
      int take = std::min(need, p.reserve[p.weapon]);
      p.mag[p.weapon] += take;
      p.reserve[p.weapon] -= take;
      p.reloadT = -1;
      audio_.play("reload", {p.pos.x, 1.2f, p.pos.y}, 0.6f, 0.8f);
    }
  } else if (firearm && (in.reloadPressed || (p.mag[p.weapon] == 0 && (in.attackPressed || in.attackHeld))) && p.reserve[p.weapon] > 0 &&
             p.mag[p.weapon] < w.magazine) {
    p.reloadT = 0;
    requestAnim(p, kActReload, animator_.actionDuration(kActReload) / w.reloadTime, true);
    audio_.play("reload", {p.pos.x, 1.2f, p.pos.y}, 0.6f);
  }

  // ---- melee in progress
  if (p.attackT >= 0) {
    p.attackT += dt;
    float hitAt = p.attackDur * (p.attackKind == 2 ? 0.5f : (p.attackKind == 3 ? w.hitTime : 0.45f));
    if (!p.attackHit && p.attackT >= hitAt) {
      p.attackHit = true;
      meleeStrike({ActorKind::Player, 0}, p.pos, p.yaw, p.attackKind == 2 ? kWpnFists : p.weapon, p.attackKind);
    }
    if (p.attackT >= p.attackDur) { p.attackT = -1; p.comboWindow = 0.45f; }
  }

  // ---- triggers
  bool trigger = in.attackPressed || (firearm && w.cls == WeaponClass::Automatic && in.attackHeld);
  if (!trigger || p.reloadT >= 0) return;
  // auto-aim: nearest sensible target in front (stick direction if moving, else facing)
  Vec2 facing = in.move.length() > 0.3f ? p.vel.normalized() : fwd2(p.yaw);
  if (facing.lengthSq() < 0.5f) facing = fwd2(p.yaw);
  if (firearm) {
    if (p.fireCooldown > 0) return;
    int t = findAimTarget(p.pos, facing, w.range * 1.2f, std::cos(40.0f * kDeg2Rad), true);
    p.aimNpc = t;
    Vec2 aim = facing;
    if (t >= 0) aim = (npcs_[t].pos - p.pos).normalized();
    p.aimDir = aim;
    p.targetYaw = p.yaw = yawFromDir(aim);
    p.aimHold = 1.4f;
    if (p.mag[p.weapon] <= 0) {
      audio_.play("empty", {p.pos.x, 1.2f, p.pos.y}, 0.6f);
      p.fireCooldown = 0.3f;
      if (p.reserve[p.weapon] <= 0) toast("Sem munição", "pistol", rgba(1.0f, 0.6f, 0.4f));
      return;
    }
    p.mag[p.weapon]--;
    p.fireCooldown = w.cooldown;
    Vec3 origin{p.pos.x + aim.x * 0.55f, p.y + 1.35f, p.pos.y + aim.y * 0.55f};
    fireWeapon({ActorKind::Player, 0}, origin, aim, p.weapon);
    playerAnim_.recoil = w.cls == WeaponClass::Shotgun ? 1.0f : (w.cls == WeaponClass::Automatic ? 0.35f : 0.6f);
    cameraShake_ = std::max(cameraShake_, w.cls == WeaponClass::Shotgun ? 0.35f : 0.12f);
    return;
  }
  // melee / unarmed: combo punch, punch, kick
  if (p.attackT >= 0) return;
  int t = findAimTarget(p.pos, facing, 3.0f, std::cos(75.0f * kDeg2Rad), true);
  if (t >= 0) p.targetYaw = p.yaw = yawFromDir(npcs_[t].pos - p.pos);
  int kind = 3;
  if (p.weapon == kWpnFists) {
    p.combo = p.comboWindow > 0 ? (p.combo + 1) % 3 : 0;
    kind = p.combo == 2 ? 2 : 1;
  }
  startMelee({ActorKind::Player, 0}, kind);
}

void Game::updateCombat(float dt, const InputFrame& in) {
  updatePlayerAttack(dt, in);
  Player& p = player_;
  // knock-back slide + getting up
  if (p.knockVel.lengthSq() > 1e-4f && p.vehicle < 0) {
    phys::moveCircle(world_, p.pos, p.knockVel * dt, 0.32f);
    p.knockVel = p.knockVel * (1.0f - expDecay(7.0f, dt));
  }
  if (p.down && !p.dead) {
    p.downT -= dt;
    if (p.downT <= 0) { p.down = false; requestAnim(p, kActStandUp, 3.2f); p.hitStun = animator_.actionDuration(kActStandUp) / 3.2f; }
  }
  if (p.dead) {
    deathT_ += dt;
    if (deathT_ > 3.0f && fadeTarget_ < 1.0f) {
      fadeTarget_ = 1.0f;
      fadeThen_ = [this]() { respawnPlayer(); };
    }
  }
  // the player's own animation flags
  playerAnim_.aimTarget = (isFirearm(p.weapon) && p.aimHold > 0 && p.reloadT < 0 && p.vehicle < 0 && !p.down) ? 1.0f : 0.0f;
  playerAnim_.twoHanded = weaponDef(p.weapon).cls == WeaponClass::Automatic || weaponDef(p.weapon).cls == WeaponClass::Shotgun;
  // events expire
  events_.erase(std::remove_if(events_.begin(), events_.end(), [&](const WorldEvent& e) { return time_ - e.time > 0.6f; }), events_.end());
  updateEffects(dt);
  updatePickups(dt);
}

// ------------------------------------------------------------------------------------------------ effects & pickups
void Game::spawnBlood(Vec3 pos, Vec2 dir, int count) {
  for (int i = 0; i < count; ++i) {
    Particle* q = particles_.acquire();
    if (!q) return;
    q->pos = pos;
    q->vel = {dir.x * rng_.range(0.5f, 2.5f) + rng_.range(-0.6f, 0.6f), rng_.range(0.2f, 1.6f), dir.y * rng_.range(0.5f, 2.5f) + rng_.range(-0.6f, 0.6f)};
    q->life = q->maxLife = rng_.range(0.35f, 0.7f);
    q->size = rng_.range(0.08f, 0.16f);
    q->color = rgba(0.32f, 0.02f, 0.02f, 0.9f);
    q->gravity = 9.0f;
  }
  if (stains_.size() < 40) stains_.push_back({{pos.x + dir.x * 0.6f, 0.02f + world_.heightAt(pos.x, pos.z), pos.z + dir.y * 0.6f}, 0.35f, 30.0f});
}

void Game::spawnImpact(Vec3 pos, int count) {
  for (int i = 0; i < count; ++i) {
    Particle* q = particles_.acquire();
    if (!q) return;
    q->pos = pos;
    q->vel = {rng_.range(-1.2f, 1.2f), rng_.range(0.4f, 2.0f), rng_.range(-1.2f, 1.2f)};
    q->life = q->maxLife = rng_.range(0.3f, 0.6f);
    q->size = rng_.range(0.12f, 0.25f);
    q->color = rgba(0.62f, 0.6f, 0.56f, 0.7f);
    q->gravity = 4.0f;
  }
}

void Game::updateEffects(float dt) {
  for (Tracer& t : tracers_) t.life -= dt;
  tracers_.erase(std::remove_if(tracers_.begin(), tracers_.end(), [](const Tracer& t) { return t.life <= 0; }), tracers_.end());
  for (Stain& s : stains_) s.life -= dt;
  stains_.erase(std::remove_if(stains_.begin(), stains_.end(), [](const Stain& s) { return s.life <= 0; }), stains_.end());
  muzzleT_ = std::max(0.0f, muzzleT_ - dt);
}

void Game::updatePickups(float dt) {
  Player& p = player_;
  for (Pickup& k : pickups_) {
    if (!k.active) {
      k.respawn -= dt;
      if (k.respawn <= 0) k.active = true;
      continue;
    }
    if (p.dead || p.vehicle >= 0) continue;
    if ((Vec2{k.pos.x, k.pos.z} - p.pos).length() < 1.1f) {
      bool had = p.owned[k.weapon];
      giveWeapon(k.weapon, k.ammo);
      markProgress(kPgArmed);
      k.active = false;
      k.respawn = 90.0f;
      audio_.play("reload", k.pos, 0.8f, 1.2f);
      const WeaponDef& d = weaponDef(k.weapon);
      toast(had ? std::string("Munição: ") + d.name : std::string("Pegou: ") + d.name, d.icon);
      if (!had) equipWeapon(k.weapon);
    }
  }
}

// Weapons in hands, pickups on the ground, tracers, muzzle flash, blood stains.
void Game::emitCombatVisuals(gfx::FrameData& fd) {
  UvRect dot = assets_.icon("dot");
  auto glow = [&](Vec3 p, Vec3 rgb, float size, float intensity) {
    if (!dot.valid) return;
    SpriteDef sd;
    sd.tex = assets_.iconsTex; sd.u0 = dot.u0; sd.v0 = dot.v0; sd.u1 = dot.u1; sd.v1 = dot.v1;
    sd.pivX = 0.5f; sd.pivY = 0.5f; sd.valid = true; sd.wm = sd.hm = size;
    addSprite(&sd, p, 1.0f, 1.0f, false, packRGBA8(rgb.x, rgb.y, rgb.z, 1.0f), false, false, intensity);
  };
  for (const Tracer& t : tracers_) {
    Vec3 d = t.b - t.a;
    float len = d.length();
    int n = std::min(40, (int)(len / 0.7f) + 1);
    for (int i = 0; i < n; ++i) glow(t.a + d * ((i + 0.5f) / n), {1.0f, 0.85f, 0.55f}, 0.09f, 6.0f * (t.life / 0.07f));
  }
  if (muzzleT_ > 0) {
    glow(muzzlePos_, {1.0f, 0.7f, 0.35f}, 0.55f, 14.0f);
    pendingLights_.push_back({muzzlePos_, {0, 0, 0}, {14.0f, 9.0f, 4.5f}, 7.0f, -2.0f, 0});
  }
  for (const Stain& s : stains_) {
    float a = std::min(1.0f, s.life / 3.0f) * 0.75f;
    addDecalEllipse(s.pos, s.r, s.r * 0.8f, a, 0, 0);
  }
  if (!weaponMeshes_.ok || player_.indoors) return;
  for (const Pickup& k : pickups_) {
    if (!k.active) continue;
    float spin = time_ * 1.6f;
    gfx::ModelDraw d;
    d.model = weaponMeshes_.mesh[k.weapon];
    d.material = weaponMeshes_.material;
    d.transform = Mat4::translation({k.pos.x, k.pos.y + 0.45f + 0.06f * std::sin(time_ * 2.5f), k.pos.z}) *
                  quatMatrix(Quat::axisAngle({0, 1, 0}, spin)) * quatMatrix(Quat::axisAngle({0, 0, 1}, 0.25f));
    d.params = {0.0f, 0.0f, 1.0f, 1.0f};
    fd.models.push_back(d);
    glow({k.pos.x, k.pos.y + 0.12f, k.pos.z}, {0.95f, 0.8f, 0.35f}, 1.1f, 1.6f + 0.6f * std::sin(time_ * 3.0f));
  }
}

}  // namespace gtabr
