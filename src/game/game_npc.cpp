#include <algorithm>
#include <cmath>

#include "game.h"

namespace gtabr {

void Game::spawnNpcs() {
  npcs_.clear();
  int id = 0;
  for (const NpcSpawn& s : world_.npcs) {
    Npc n;
    n.id = id++;
    n.archetype = s.archetype;
    n.role = s.role;
    n.pos = {s.pos.x, s.pos.z};
    n.home = n.pos;
    n.yaw = s.yaw;
    n.interior = s.interior;
    n.walkSpeed = rng_.range(1.05f, 1.65f);
    n.stationary = s.role != 0;
    n.state = n.stationary ? NpcState::Work : NpcState::Idle;
    n.stateTimer = rng_.range(0.2f, 4.0f);
    n.y = world_.heightAt(n.pos.x, n.pos.y);
    n.animTime = rng_.uni();
    n.lookYaw = n.yaw;
    n.greetCooldown = rng_.range(0.0f, 12.0f);
    npcs_.push_back(n);
  }
}

std::string Game::chatLine(const Npc& n) {
  static const char* generic[] = {
      "Esse calor tá de matar, né?", "Cuidado com o trânsito na avenida!", "O pão do Mercado do Zé tá saindo quentinho.",
      "Se o carro der problema, a Oficina Silva resolve.", "O posto aqui do bairro tá com preço bom hoje.",
      "Esse bairro era mais tranquilo antes.", "Vai chover mais tarde, pode apostar.", "Já viu o movimento na praça hoje?"};
  static const char* runner[] = {"Tô treinando pra maratona!", "Corre comigo?", "Hidratação é tudo!"};
  if (n.archetype == "corredor") return runner[rng_.irange(0, 2)];
  return generic[rng_.irange(0, 7)];
}

void Game::updateNpcs(float dt) {
  stats_.npcNear = stats_.npcMid = stats_.npcFar = 0;
  Vec2 ref = player_.pos;
  int pathBudget = 3;
  for (Npc& n : npcs_) {
    if (n.interior != player_.indoors && n.stationary) { n.bubbleTimer = 0; continue; }
    if (n.interior != player_.indoors) continue;
    float d = (n.pos - ref).length();
    int lod = d < 45.0f ? 0 : (d < 100.0f ? 1 : 2);
    n.lod = lod;
    n.bubbleTimer = std::max(0.0f, n.bubbleTimer - dt);
    n.greetCooldown = std::max(0.0f, n.greetCooldown - dt);
    n.accumDt += dt;
    float period = lod == 0 ? 0.0f : (lod == 1 ? 0.25f : 1.0f);
    if (n.accumDt < period) { (lod == 1 ? stats_.npcMid : stats_.npcFar)++; continue; }
    float step = n.accumDt;
    n.accumDt = 0;
    (lod == 0 ? stats_.npcNear : (lod == 1 ? stats_.npcMid : stats_.npcFar))++;
    if (n.repath > 0) n.repath -= step;
    // path budget: expensive A* calls are spread over frames
    if (n.state == NpcState::Idle && n.stateTimer - step <= 0 && !n.stationary) {
      if (pathBudget <= 0) { n.stateTimer = 0.05f; continue; }
      --pathBudget;
    }
    npcThink(n, step);
  }
}

void Game::npcThink(Npc& n, float dt) {
  const NavMesh& nav = navFor(n);
  n.stateTimer -= dt;
  Vec2 toPlayer = player_.pos - n.pos;
  float pd = toPlayer.length();

  // world danger: a fast vehicle heading at us
  if (!n.stationary && n.state != NpcState::Flee && n.state != NpcState::Stunned && !n.interior) {
    for (const Vehicle& v : vehicles_) {
      float sp = v.vel.length();
      if (sp < 5.0f) continue;
      Vec2 rel = n.pos - v.pos;
      float dist = rel.length();
      if (dist < 11.0f && rel.dot(v.vel) / (dist * sp + 1e-4f) > 0.75f) {
        n.state = NpcState::Flee;
        n.stateTimer = 1.8f;
        n.path.clear();
        Vec2 side = v.vel.normalized().perp();
        if (side.dot(rel) < 0) side = -side;
        n.path.push_back(n.pos + side * 5.0f);
        n.pathIdx = 0;
        n.bubble = "Eita!";
        n.bubbleTimer = 1.2f;
        break;
      }
    }
  }

  switch (n.state) {
    case NpcState::Work: {
      // stationary roles: face their post, look at the player when close
      if (pd < 5.0f) n.lookYaw = yawFromDir(toPlayer);
      n.yaw = lerpAngle(n.yaw, n.lookYaw, expDecay(6.0f, dt));
      n.speed = 0;
      if (n.greetCooldown <= 0 && pd < 4.5f) {
        n.greetCooldown = 25.0f;
        n.bubble = n.role == 1 ? "Boa tarde!" : (n.role == 2 ? "Fala, chefe!" : (n.role == 4 ? "Bem-vindo!" : "Opa, tudo bem?"));
        n.bubbleTimer = 2.2f;
      }
      break;
    }
    case NpcState::Talk: {
      n.speed = 0;
      n.yaw = lerpAngle(n.yaw, yawFromDir(toPlayer), expDecay(8.0f, dt));
      break;
    }
    case NpcState::Stunned: {
      n.speed = 0;
      if (n.stateTimer <= 0) { n.state = NpcState::Idle; n.stateTimer = 1.0f; }
      break;
    }
    case NpcState::Idle: {
      n.speed = 0;
      if (pd < 3.2f && n.greetCooldown <= 0) {
        n.lookYaw = yawFromDir(toPlayer);
        n.greetCooldown = 22.0f;
        if (rng_.chance(0.45f)) {
          static const char* hi[] = {"Opa!", "Bom dia!", "E aí?", "Boa tarde!", "Fala!"};
          n.bubble = hi[rng_.irange(0, 4)];
          n.bubbleTimer = 1.8f;
        }
      }
      if (n.stateTimer < 6.0f) n.yaw = lerpAngle(n.yaw, n.lookYaw, expDecay(5.0f, dt));
      if (n.stateTimer <= 0) {
        // choose a destination not too far away
        for (int tries = 0; tries < 6; ++tries) {
          Vec2 dest;
          if (!nav.randomPoint(rng_, dest)) break;
          if ((dest - n.pos).length() > 70.0f || (dest - n.pos).length() < 6.0f) continue;
          std::vector<Vec2> path;
          if (nav.findPath(n.pos, dest, path) && !path.empty()) {
            n.path = std::move(path);
            n.pathIdx = 0;
            n.state = NpcState::Walk;
            n.stateTimer = 90.0f;
            break;
          }
        }
        if (n.state == NpcState::Idle) n.stateTimer = rng_.range(1.0f, 3.0f);
      }
      break;
    }
    case NpcState::Walk:
    case NpcState::Flee: {
      float speed = n.state == NpcState::Flee ? 4.2f : n.walkSpeed;
      if (n.pathIdx >= n.path.size()) {
        n.state = NpcState::Idle;
        n.stateTimer = rng_.range(2.0f, 9.0f);
        n.lookYaw = rng_.range(0, kTau);
        n.speed = 0;
        break;
      }
      Vec2 target = n.path[n.pathIdx];
      Vec2 to = target - n.pos;
      float dist = to.length();
      if (dist < 0.45f) { ++n.pathIdx; break; }
      Vec2 dir = to / dist;
      if (n.lod == 0) {
        // separation from other pedestrians and the player
        Vec2 sep;
        for (const Npc& o : npcs_) {
          if (&o == &n || o.interior != n.interior) continue;
          Vec2 d = n.pos - o.pos;
          float l = d.length();
          if (l < 0.9f && l > 1e-4f) sep += d / l * (0.9f - l);
        }
        Vec2 dp = n.pos - player_.pos;
        float lp = dp.length();
        if (lp < 1.1f && lp > 1e-4f) sep += dp / lp * (1.1f - lp) * 1.2f;
        dir = (dir + sep * 0.9f).normalized();
      }
      Vec2 step = dir * speed * dt;
      Vec2 before = n.pos;
      if (n.lod == 0 || n.lod == 1) phys::moveCircle(world_, n.pos, step, 0.28f);
      else n.pos += step;   // far NPCs are abstract: they follow the path without collision
      float moved = (n.pos - before).length();
      n.speed = moved / std::max(dt, 1e-4f);
      if (moved < speed * dt * 0.25f) {
        n.blockedTime += dt;
        if (n.blockedTime > 1.2f && n.state == NpcState::Walk) { n.state = NpcState::Idle; n.stateTimer = rng_.range(0.5f, 2.0f); n.path.clear(); n.blockedTime = 0; }
      } else n.blockedTime = 0;
      if (n.speed > 0.1f) n.yaw = lerpAngle(n.yaw, yawFromDir(dir), expDecay(10.0f, dt));
      n.animTime += n.speed * dt / (n.state == NpcState::Flee ? 2.2f : 1.5f);
      if (n.state == NpcState::Flee && n.stateTimer <= 0) { n.state = NpcState::Idle; n.stateTimer = 1.5f; n.path.clear(); }
      break;
    }
  }
  n.y += (world_.heightAt(n.pos.x, n.pos.y) - n.y) * expDecay(15.0f, dt);
}

}  // namespace gtabr
