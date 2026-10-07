// Pedestrian AI: a state machine driven by personality (bravery, temper), fear and perceived world events.
// Near NPCs run the full logic every frame; mid/far NPCs are stepped at a reduced rate with cheap movement.
#include <algorithm>
#include <cmath>

#include "game.h"

namespace gtabr {

namespace {
uint32_t mix32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
float h01(uint32_t x) { return (mix32(x) & 0xFFFFFF) / 16777215.0f; }
bool isWorker(const Npc& n) { return n.stationary && !n.police; }
}  // namespace

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
    // personality: most people are cautious, a few are brave or short-tempered
    n.bravery = std::pow(h01(n.id * 97u + 13u), 1.6f);
    n.temper = std::pow(h01(n.id * 131u + 7u), 2.2f);
    if (n.archetype == "corredor" || n.archetype == "jovem_moletom") n.bravery = std::min(1.0f, n.bravery + 0.35f);
    if (n.role == 2) n.bravery = std::max(n.bravery, 0.7f);   // the mechanic does not run from a fist fight
    n.mood = n.temper > 0.6f ? Mood::Angry : (n.bravery > 0.5f ? Mood::Friendly : Mood::Calm);
    n.lineIdx = (int)(h01(n.id * 7u) * 7.0f);
    npcs_.push_back(n);
  }
}

// ------------------------------------------------------------------------------------------------ dialogue
// context: 0 greeting, 1 city comment, 2 scared/hurt, 3 angry/threat, 4 player armed, 5 gunshots, 6 police around,
// 7 accident, 8 on the phone, 9 chatting, 10 pushed/provoked, 11 player is wanted, 12 knocked out wakes up
std::string Game::npcLine(Npc& n, int context) {
  static const std::vector<std::vector<const char*>> lines = {
      {"Opa, tudo bem?", "Bom dia, vizinho!", "E aí, beleza?", "Fala, parceiro!", "Boa tarde!", "Opa! Prazer."},
      {"Esse calor tá de matar, né?", "O pão do Mercado do Zé sai quentinho às seis.", "Se o carro der problema, a Oficina Silva resolve.",
       "O posto aqui do bairro tá com preço bom hoje.", "Esse bairro era mais tranquilo antes.", "Vai chover mais tarde, pode apostar.",
       "Já viu o movimento na praça hoje?", "Cuidado com o trânsito na avenida!", "Meu primo jura que viu um disco voador ali na praça."},
      {"Socorro!", "Me deixa em paz!", "Para com isso!", "Alguém me ajuda!", "Não, não, não!", "Tá louco?!"},
      {"Vem, se for homem!", "Tá procurando confusão?", "Vai se arrepender disso!", "Agora você vai ver!", "Mexeu com a pessoa errada!"},
      {"Abaixa essa arma, pelo amor de Deus!", "Calma aí, calma aí...", "Eu não quero problema!", "Se afasta de mim!", "Tá armado?!"},
      {"Tiro! Corre!", "Abaixa todo mundo!", "Meu Deus, foi tiro!", "Sai da rua, sai da rua!", "Tão atirando!"},
      {"Olha a polícia...", "Eita, deu ruim pra alguém.", "Polícia! Sai da frente!", "Melhor não me meter nisso."},
      {"Meu Deus, que batida!", "Alguém chama a ambulância!", "Esse aí dirige muito mal!", "Eita! Tá todo mundo bem?"},
      {"Alô, polícia? Tem um cara causando aqui...", "Alô? 190? Aconteceu uma coisa aqui na rua!", "Polícia? Rápido, por favor!"},
      {"...e aí eu falei pra ele: não dá!", "Sério? Não acredito!", "Pois é, a vida tá difícil.", "Hahaha, é mesmo!",
       "E o seu filho, tá bem?", "Você viu o jogo ontem?"},
      {"Ei! Tá me empurrando por quê?", "Olha por onde anda!", "Quer apanhar?", "Encosta em mim de novo pra você ver!"},
      {"Ei, você não é aquele que a polícia tá procurando?", "Sai daqui, não quero confusão com polícia.", "Vou chamar a polícia, hein!"},
      {"Ai... minha cabeça...", "O que aconteceu?", "Nunca mais saio de casa..."},
  };
  const auto& l = lines[clamp(context, 0, (int)lines.size() - 1)];
  n.lineIdx = (n.lineIdx + 1 + (int)(rng_.uni() * 2.0f)) % (int)l.size();
  return l[n.lineIdx];
}

void Game::npcSay(Npc& n, const std::string& line, float secs) {
  n.bubble = line;
  n.bubbleTimer = secs;
}

void Game::npcStartFlee(Npc& n, Vec2 from, float secs) {
  if (isWorker(n) && n.bravery > 0.5f) { n.state = NpcState::Alert; n.alertT = 3.0f; n.threatPos = from; return; }
  Vec2 away = n.pos - from;
  if (away.lengthSq() < 1e-3f) away = fwd2(rng_.range(0.0f, kTau));
  away = away.normalized();
  const NavMesh& nav = navFor(n);
  // flee to a reachable point away from the danger
  std::vector<Vec2> path;
  Vec2 dest = n.pos + away * rng_.range(16.0f, 26.0f) + away.perp() * rng_.range(-6.0f, 6.0f);
  if (!nav.findPath(n.pos, dest, path) || path.empty()) path = {n.pos + away * 6.0f};
  n.path = std::move(path);
  n.pathIdx = 0;
  n.state = NpcState::Flee;
  n.stateTimer = secs;
  n.threatPos = from;
  n.chatWith = -1;
}

// ------------------------------------------------------------------------------------------------ perception
void Game::perceiveEvents(Npc& n) {
  for (const WorldEvent& e : events_) {
    if (e.time <= n.lastEventT) continue;
    float d = (e.pos - n.pos).length();
    if (d > e.radius) continue;
    if (e.victim.kind == ActorKind::Npc && e.victim.index == n.id) continue;   // the victim already reacted
    if (e.instigator.kind == ActorKind::Npc && e.instigator.index == n.id) continue;
    bool byPlayer = e.instigator.kind == ActorKind::Player;
    bool byCop = e.instigator.kind == ActorKind::Npc && npcs_[e.instigator.index].police;
    bool sees = d < 4.0f || lineOfSight(n.pos, e.pos);
    float closeness = 1.0f - d / e.radius;
    switch (e.kind) {
      case EventKind::Gunshot: {
        n.fear = std::min(1.0f, n.fear + 0.5f + 0.5f * closeness);
        npcSay(n, npcLine(n, 5), 1.8f);
        if (isWorker(n)) { n.state = NpcState::Cower; n.stateTimer = 6.0f; break; }
        if (d < 7.0f && n.bravery < 0.3f) { n.state = NpcState::Cower; n.stateTimer = rng_.range(4.0f, 8.0f); n.threatPos = e.pos; }
        else npcStartFlee(n, e.pos, rng_.range(8.0f, 14.0f));
        if (byPlayer && n.reportT < 0 && rng_.chance(0.6f)) { n.reportT = rng_.range(5.0f, 9.0f); n.reportPos = e.pos; n.reportSeverity = 1.0f + (sees ? 0.5f : 0.0f); }
        break;
      }
      case EventKind::Assault:
      case EventKind::CopAssault:
      case EventKind::Kill: {
        if (!sees) break;
        n.fear = std::min(1.0f, n.fear + (e.kind == EventKind::Kill ? 0.8f : 0.35f) * (0.5f + closeness));
        if (byCop) { npcSay(n, npcLine(n, 6)); break; }
        bool playerArmed = byPlayer && player_.weapon != kWpnFists;
        // a brave passer-by may step in against an unarmed brawler (not against a killer)
        if (byPlayer && !playerArmed && e.kind == EventKind::Assault && n.bravery > 0.78f && !isWorker(n) && d < 9.0f &&
            n.state != NpcState::Fight) {
          n.state = NpcState::Fight;
          n.target = e.instigator;
          n.stateTimer = 14.0f;
          npcSay(n, npcLine(n, 3));
          break;
        }
        npcSay(n, npcLine(n, e.kind == EventKind::Kill ? 2 : 6), 2.0f);
        if (n.state != NpcState::Fight) {
          if (isWorker(n)) { n.state = NpcState::Alert; n.alertT = 5.0f; n.threatPos = e.pos; }
          else if (n.fear > 0.45f || e.kind == EventKind::Kill) npcStartFlee(n, e.pos, rng_.range(8.0f, 16.0f));
          else { n.state = NpcState::Alert; n.alertT = 3.5f; n.threatPos = e.pos; }
        }
        if (byPlayer && n.reportT < 0 && rng_.chance(e.kind == EventKind::Kill ? 0.9f : 0.5f)) {
          n.reportT = rng_.range(4.0f, 8.0f);
          n.reportPos = e.pos;
          n.reportSeverity = e.severity;
        }
        break;
      }
      case EventKind::Crash:
      case EventKind::RunOver: {
        if (!sees) break;
        npcSay(n, npcLine(n, 7), 2.0f);
        if (n.state == NpcState::Idle || n.state == NpcState::Walk || n.state == NpcState::Chat) {
          n.state = NpcState::Alert; n.alertT = 3.0f; n.threatPos = e.pos;
        }
        if (e.kind == EventKind::RunOver && byPlayer && n.reportT < 0 && rng_.chance(0.6f)) {
          n.reportT = rng_.range(5.0f, 9.0f); n.reportPos = e.pos; n.reportSeverity = 1.0f;
        }
        break;
      }
      case EventKind::WeaponDrawn: {
        if (!sees || !byPlayer) break;
        n.fear = std::min(1.0f, n.fear + 0.3f);
        npcSay(n, npcLine(n, 4), 2.0f);
        if (!isWorker(n) && n.bravery < 0.6f && (n.state == NpcState::Idle || n.state == NpcState::Walk || n.state == NpcState::Chat))
          npcStartFlee(n, e.pos, 5.0f);
        if (isFirearm(player_.weapon) && n.reportT < 0 && rng_.chance(0.25f)) {
          n.reportT = rng_.range(6.0f, 10.0f); n.reportPos = e.pos; n.reportSeverity = 0.4f;
        }
        break;
      }
    }
  }
  if (!events_.empty()) n.lastEventT = std::max(n.lastEventT, events_.back().time);
}

// ------------------------------------------------------------------------------------------------ fighting
void Game::npcFight(Npc& n, float dt) {
  if (!actorAlive(n.target) || (n.target.kind == ActorKind::Npc && npcs_[n.target.index].state == NpcState::Down) ||
      (n.target.kind == ActorKind::Player && (player_.down || player_.vehicle >= 0))) {
    n.state = NpcState::Idle;
    n.stateTimer = rng_.range(1.0f, 3.0f);
    n.attackT = -1;
    if (n.target.kind == ActorKind::Player && player_.down) npcSay(n, "Fica esperto da próxima vez!", 2.0f);
    return;
  }
  // losing badly: run
  if (n.health < 35.0f && !n.police) { npcStartFlee(n, actorPos(n.target), 12.0f); npcSay(n, npcLine(n, 2)); return; }
  n.stateTimer -= dt;
  Vec2 tp = actorPos(n.target);
  Vec2 to = tp - n.pos;
  float dist = to.length();
  if (dist > 16.0f || n.stateTimer <= 0) { n.state = NpcState::Idle; n.stateTimer = 2.0f; return; }
  n.yaw = lerpAngle(n.yaw, yawFromDir(to), expDecay(10.0f, dt));
  n.lookYaw = n.yaw;
  // attack in progress
  if (n.attackT >= 0) {
    n.attackT += dt;
    n.speed = 0;
    float hitAt = n.attackDur * (n.attackKind == 2 ? 0.5f : 0.45f);
    if (!n.attackHit && n.attackT >= hitAt) {
      n.attackHit = true;
      meleeStrike({ActorKind::Npc, n.id}, n.pos, n.yaw, n.attackKind == 2 ? kWpnFists : n.weapon, n.attackKind);
    }
    if (n.attackT >= n.attackDur) { n.attackT = -1; n.attackCd = rng_.range(0.5f, 1.3f); }
    return;
  }
  n.attackCd -= dt;
  float reach = weaponDef(n.weapon).range * 0.85f;
  if (dist > reach) {
    // close in (straight line with wall sliding; fights happen at short range)
    Vec2 dir = to / std::max(dist, 1e-3f);
    Vec2 before = n.pos;
    phys::moveCircle(world_, n.pos, dir * (dist > 5.0f ? 3.6f : 2.4f) * dt, 0.3f);
    n.speed = (n.pos - before).length() / std::max(dt, 1e-4f);
  } else {
    n.speed = 0;
    if (n.attackCd <= 0) startMelee({ActorKind::Npc, n.id}, n.weapon == kWpnFists ? (rng_.chance(0.25f) ? 2 : 1) : 3);
  }
}

// ------------------------------------------------------------------------------------------------ update
void Game::updateNpcs(float dt) {
  stats_.npcNear = stats_.npcMid = stats_.npcFar = 0;
  Vec2 ref = player_.pos;
  int pathBudget = 3;
  for (Npc& n : npcs_) {
    if (n.despawn) continue;
    if (n.interior != player_.indoors && n.stationary) { n.bubbleTimer = 0; continue; }
    if (n.interior != player_.indoors) continue;
    float d = (n.pos - ref).length();
    int lod = d < 45.0f ? 0 : (d < 100.0f ? 1 : 2);
    // anyone involved in action runs at full rate regardless of distance
    if (n.police || n.state == NpcState::Fight || n.state == NpcState::Flee || n.hitStun > 0 || n.attackT >= 0) lod = std::min(lod, 1);
    n.lod = lod;
    n.bubbleTimer = std::max(0.0f, n.bubbleTimer - dt);
    n.greetCooldown = std::max(0.0f, n.greetCooldown - dt);
    // witnesses finish their phone call even while running
    if (n.reportT >= 0 && n.state != NpcState::Dead && n.state != NpcState::Down) {
      n.reportT -= dt;
      if (n.reportT < 0) { reportCrime(n.reportPos, n.reportSeverity, false); n.reportT = -1; }
    }
    n.accumDt += dt;
    float period = lod == 0 ? 0.0f : (lod == 1 ? 0.25f : 1.0f);
    if (n.accumDt < period) { (lod == 1 ? stats_.npcMid : stats_.npcFar)++; continue; }
    float step = n.accumDt;
    n.accumDt = 0;
    (lod == 0 ? stats_.npcNear : (lod == 1 ? stats_.npcMid : stats_.npcFar))++;
    if (n.repath > 0) n.repath -= step;
    if (n.state == NpcState::Idle && n.stateTimer - step <= 0 && !n.stationary) {
      if (pathBudget <= 0) { n.stateTimer = 0.05f; continue; }
      --pathBudget;
    }
    if (n.police) copThink(n, step);
    else npcThink(n, step);
  }
}

void Game::npcThink(Npc& n, float dt) {
  const NavMesh& nav = navFor(n);
  n.stateTimer -= dt;
  Vec2 toPlayer = player_.pos - n.pos;
  float pd = toPlayer.length();
  n.fear = std::max(0.0f, n.fear - dt * 0.05f);

  // ---- bodies on the floor
  if (n.state == NpcState::Dead) {
    n.speed = 0;
    n.deadT += dt;
    // population upkeep: a corpse out of sight is recycled as a new pedestrian elsewhere
    if (n.deadT > 40.0f && pd > 60.0f && !n.stationary) {
      Vec2 p;
      for (int t = 0; t < 8; ++t)
        if (nav.randomPoint(rng_, p) && (p - player_.pos).length() > 60.0f) {
          n.pos = p; n.health = 100; n.state = NpcState::Idle; n.stateTimer = 1; n.fear = 0; n.deadT = 0; n.animReq = -1;
          n.knockVel = {}; n.threat = {}; n.path.clear();
          break;
        }
    }
    return;
  }
  if (n.knockVel.lengthSq() > 1e-4f) {
    phys::moveCircle(world_, n.pos, n.knockVel * dt, 0.3f);
    n.knockVel = n.knockVel * (1.0f - expDecay(7.0f, dt));
  }
  if (n.state == NpcState::Down) {
    n.speed = 0;
    n.downT -= dt;
    if (n.downT <= 0) {
      requestAnim(n, kActStandUp, 3.2f);
      n.health = std::max(n.health, 35.0f);
      n.hitStun = animator_.actionDuration(kActStandUp) / 3.2f;
      npcSay(n, npcLine(n, 12), 2.5f);
      if (n.threat.valid()) npcStartFlee(n, actorPos(n.threat), 12.0f);
      else { n.state = NpcState::Idle; n.stateTimer = 2.0f; }
    }
    return;
  }
  if (n.hitStun > 0) { n.hitStun -= dt; n.speed = 0; return; }
  if (n.lod == 0) perceiveEvents(n);

  // a fast vehicle heading at us
  if (!n.stationary && n.state != NpcState::Flee && n.state != NpcState::Fight && n.state != NpcState::Stunned && !n.interior) {
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
        npcSay(n, "Eita!", 1.2f);
        break;
      }
    }
  }
  // a police chase passing by: people stop, look and step aside
  if (wanted_ > 0 && n.lod == 0 && (n.state == NpcState::Idle || n.state == NpcState::Walk || n.state == NpcState::Chat) &&
      n.greetCooldown <= 0) {
    for (const Npc& c : npcs_) {
      if (!c.police || c.despawn || (c.state != NpcState::CopChase && c.state != NpcState::Fight)) continue;
      if ((c.pos - n.pos).length() > 9.0f) continue;
      n.greetCooldown = 12.0f;
      npcSay(n, npcLine(n, 6), 2.0f);
      n.state = NpcState::Alert;
      n.alertT = 2.5f;
      n.threatPos = c.pos;
      break;
    }
  }
  // the player walks around with a gun out / is wanted: people keep their distance
  if ((n.state == NpcState::Idle || n.state == NpcState::Walk) && pd < 6.0f && !n.stationary) {
    if (player_.aimHold > 0 && isFirearm(player_.weapon) && lineOfSight(n.pos, player_.pos)) {
      npcSay(n, npcLine(n, 4));
      npcStartFlee(n, player_.pos, 6.0f);
    } else if (wanted_ >= 2 && n.greetCooldown <= 0) {
      n.greetCooldown = 15.0f;
      npcSay(n, npcLine(n, 11));
      if (n.bravery < 0.5f) npcStartFlee(n, player_.pos, 5.0f);
    }
  }

  switch (n.state) {
    case NpcState::Work: {
      if (pd < 5.0f) n.lookYaw = yawFromDir(toPlayer);
      n.yaw = lerpAngle(n.yaw, n.lookYaw, expDecay(6.0f, dt));
      n.speed = 0;
      if (n.greetCooldown <= 0 && pd < 4.5f && player_.weapon == kWpnFists) {
        n.greetCooldown = 25.0f;
        npcSay(n, n.role == 1 ? "Boa tarde!" : (n.role == 2 ? "Fala, chefe!" : (n.role == 4 ? "Bem-vindo!" : "Opa, tudo bem?")), 2.2f);
      }
      break;
    }
    case NpcState::Talk: {
      n.speed = 0;
      n.yaw = lerpAngle(n.yaw, yawFromDir(toPlayer), expDecay(8.0f, dt));
      if (!panel_.open) { n.state = n.stationary ? NpcState::Work : NpcState::Idle; n.stateTimer = 2.0f; }
      break;
    }
    case NpcState::Stunned: {
      n.speed = 0;
      if (n.stateTimer <= 0) { n.state = NpcState::Idle; n.stateTimer = 1.0f; }
      break;
    }
    case NpcState::Alert: {
      n.speed = 0;
      n.alertT -= dt;
      n.lookYaw = yawFromDir(n.threatPos - n.pos);
      n.yaw = lerpAngle(n.yaw, n.lookYaw, expDecay(6.0f, dt));
      if (n.alertT <= 0) {
        if (n.fear > 0.6f && !n.stationary) npcStartFlee(n, n.threatPos, 8.0f);
        else { n.state = n.stationary ? NpcState::Work : NpcState::Idle; n.stateTimer = rng_.range(1.0f, 3.0f); }
      }
      break;
    }
    case NpcState::Cower: {
      n.speed = 0;
      n.yaw = lerpAngle(n.yaw, yawFromDir(n.threatPos - n.pos), expDecay(4.0f, dt));
      if (n.stateTimer <= 0 && n.fear < 0.5f) { n.state = n.stationary ? NpcState::Work : NpcState::Idle; n.stateTimer = 2.0f; }
      else if (n.stateTimer <= 0 && !n.stationary) npcStartFlee(n, n.threatPos, 10.0f);
      break;
    }
    case NpcState::Fight: npcFight(n, dt); break;
    case NpcState::CallPolice: {
      n.speed = 0;
      if (n.reportT < 0) { n.state = NpcState::Idle; n.stateTimer = 3.0f; }
      break;
    }
    case NpcState::Chat: {
      n.speed = 0;
      if (n.chatWith >= 0 && n.chatWith < (int)npcs_.size()) {
        Npc& o = npcs_[n.chatWith];
        n.yaw = lerpAngle(n.yaw, yawFromDir(o.pos - n.pos), expDecay(5.0f, dt));
        if (o.state != NpcState::Chat || o.chatWith != n.id) n.stateTimer = std::min(n.stateTimer, 0.0f);
        if (n.bubbleTimer <= 0 && rng_.chance(dt * 0.25f)) npcSay(n, npcLine(n, 9), 2.6f);
      }
      if (n.stateTimer <= 0) { n.state = NpcState::Idle; n.stateTimer = rng_.range(1.0f, 4.0f); n.chatWith = -1; }
      break;
    }
    case NpcState::Idle: {
      n.speed = 0;
      if (pd < 3.2f && n.greetCooldown <= 0 && player_.weapon == kWpnFists) {
        n.lookYaw = yawFromDir(toPlayer);
        n.greetCooldown = 22.0f;
        if (rng_.chance(0.5f)) npcSay(n, npcLine(n, wanted_ > 0 ? 11 : 0), 1.8f);
      }
      if (n.stateTimer < 6.0f) n.yaw = lerpAngle(n.yaw, n.lookYaw, expDecay(5.0f, dt));
      // strike up a conversation with someone nearby who is also idle
      if (n.lod == 0 && n.chatWith < 0 && rng_.chance(dt * 0.15f)) {
        for (Npc& o : npcs_) {
          if (&o == &n || o.police || o.state != NpcState::Idle || o.interior != n.interior || o.despawn) continue;
          if ((o.pos - n.pos).length() > 3.0f) continue;
          float dur = rng_.range(6.0f, 14.0f);
          n.state = o.state = NpcState::Chat;
          n.chatWith = o.id; o.chatWith = n.id;
          n.stateTimer = o.stateTimer = dur;
          npcSay(n, npcLine(n, 0), 2.0f);
          // two short-tempered people sometimes end up fighting
          if (n.temper > 0.8f && o.temper > 0.5f && rng_.chance(0.35f)) {
            n.state = NpcState::Fight; n.target = {ActorKind::Npc, o.id}; n.stateTimer = 10.0f;
            o.state = NpcState::Fight; o.target = {ActorKind::Npc, n.id}; o.stateTimer = 10.0f;
            npcSay(n, npcLine(n, 3)); npcSay(o, npcLine(o, 10));
          }
          break;
        }
        if (n.state != NpcState::Idle) break;
      }
      if (n.stateTimer <= 0) {
        // choose a destination: a point of interest of the neighbourhood or a random reachable spot
        for (int tries = 0; tries < 6; ++tries) {
          Vec2 dest;
          if (!n.interior && !world_.poiList.empty() && rng_.chance(0.35f)) {
            const Vec3& poi = world_.poiList[rng_.irange(0, (int)world_.poiList.size() - 1)];
            dest = Vec2{poi.x, poi.z} + Vec2{rng_.range(-3.0f, 3.0f), rng_.range(-3.0f, 3.0f)};
          } else if (!nav.randomPoint(rng_, dest)) break;
          if ((dest - n.pos).length() > 80.0f || (dest - n.pos).length() < 6.0f) continue;
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
      bool fleeing = n.state == NpcState::Flee;
      float speed = fleeing ? 4.6f + 0.8f * n.fear : n.walkSpeed;
      if (n.pathIdx >= n.path.size()) {
        if (fleeing && n.fear > 0.55f && n.stateTimer > 0) { npcStartFlee(n, n.threatPos, n.stateTimer); break; }
        n.state = fleeing && n.fear > 0.4f && n.bravery < 0.3f ? NpcState::Cower : NpcState::Idle;
        n.stateTimer = rng_.range(2.0f, 9.0f);
        n.lookYaw = fleeing ? yawFromDir(n.threatPos - n.pos) : rng_.range(0, kTau);
        n.speed = 0;
        break;
      }
      Vec2 target = n.path[n.pathIdx];
      Vec2 to = target - n.pos;
      float dist = to.length();
      if (dist < 0.45f) { ++n.pathIdx; break; }
      Vec2 dir = to / dist;
      if (n.lod == 0) {
        Vec2 sep;
        for (const Npc& o : npcs_) {
          if (&o == &n || o.interior != n.interior || o.state == NpcState::Dead || o.despawn) continue;
          Vec2 d = n.pos - o.pos;
          float l = d.length();
          if (l < 0.9f && l > 1e-4f) sep += d / l * (0.9f - l);
        }
        Vec2 dp = n.pos - player_.pos;
        float lp = dp.length();
        if (lp < 1.1f && lp > 1e-4f) sep += dp / lp * (1.1f - lp) * 1.2f;
        dir = (dir + sep * 0.9f).normalized();
      }
      Vec2 stepv = dir * speed * dt;
      Vec2 before = n.pos;
      if (n.lod <= 1) phys::moveCircle(world_, n.pos, stepv, 0.28f);
      else n.pos += stepv;
      float moved = (n.pos - before).length();
      n.speed = moved / std::max(dt, 1e-4f);
      // stuck: re-plan instead of freezing in place
      if (moved < speed * dt * 0.25f) {
        n.blockedTime += dt;
        if (n.blockedTime > 1.0f) {
          n.blockedTime = 0;
          if (fleeing) npcStartFlee(n, n.threatPos, n.stateTimer);
          else { n.state = NpcState::Idle; n.stateTimer = rng_.range(0.3f, 1.5f); n.path.clear(); }
        }
      } else n.blockedTime = 0;
      if (n.speed > 0.1f) n.yaw = lerpAngle(n.yaw, yawFromDir(dir), expDecay(10.0f, dt));
      n.animTime += n.speed * dt / (fleeing ? 2.2f : 1.5f);
      if (fleeing && n.stateTimer <= 0 && n.fear < 0.5f) { n.state = NpcState::Idle; n.stateTimer = 1.5f; n.path.clear(); }
      break;
    }
    default: break;
  }
  n.y += (world_.heightAt(n.pos.x, n.pos.y) - n.y) * expDecay(15.0f, dt);
}

}  // namespace gtabr
