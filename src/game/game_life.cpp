// City life: opening hours, traffic by the hour, pedestrians that keep their distance, birds and one street event at a time.
#include <algorithm>
#include <cmath>

#include "game.h"
#include "ui_theme.h"

namespace gtabr {

namespace {
float hash01u(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return (float)(x & 0xFFFFFF) / 16777216.0f;
}
}  // namespace

// ------------------------------------------------------------------------------------------------ opening hours
bool Game::shopOpen(int shopId) const {
  if (shopId < 0 || shopId >= (int)world_.shops.size()) return true;
  float h = timeOfDay_;
  switch (world_.shops[shopId].kind) {
    case ShopKind::Mercado: return h >= 7.0f && h < 22.0f;
    case ShopKind::Padaria: return h >= 6.0f && h < 19.0f;
    case ShopKind::Ferragens: return h >= 8.0f && h < 18.0f;
    default: return true;   // the convenience store never closes
  }
}

std::string Game::shopHoursLabel(int shopId) const {
  if (shopId < 0 || shopId >= (int)world_.shops.size()) return "";
  switch (world_.shops[shopId].kind) {
    case ShopKind::Mercado: return "Abre às 7h • fecha às 22h";
    case ShopKind::Padaria: return "Abre às 6h • fecha às 19h";
    case ShopKind::Ferragens: return "Abre às 8h • fecha às 18h";
    default: return "Aberto 24 horas";
  }
}

float Game::trafficFactor() const {
  float h = timeOfDay_;
  if (h < 5.0f) return 0.35f;
  if (h < 7.0f) return 0.75f;
  if (h < 10.0f) return 1.3f;     // morning rush
  if (h < 16.0f) return 1.0f;
  if (h < 20.0f) return 1.4f;     // evening rush
  if (h < 23.0f) return 0.7f;
  return 0.4f;
}

// ------------------------------------------------------------------------------------------------ update
void Game::updateLife(float dt) {
  if (phase_ != Phase::Playing) return;
  // ---- pedestrians keep a little personal space: walkers sidestep each other instead of overlapping
  {
    const float far2 = 55.0f * 55.0f;
    for (size_t i = 0; i < npcs_.size(); ++i) {
      Npc& a = npcs_[i];
      if (a.despawn || a.stationary || a.interior != player_.indoors || (a.pos - player_.pos).lengthSq() > far2) continue;
      if (a.state != NpcState::Walk && a.state != NpcState::Idle && a.state != NpcState::Flee) continue;
      for (size_t j = i + 1; j < npcs_.size(); ++j) {
        Npc& b = npcs_[j];
        if (b.despawn || b.interior != a.interior || b.state == NpcState::Dead || b.state == NpcState::Down) continue;
        Vec2 d = a.pos - b.pos;
        float l2 = d.lengthSq();
        if (l2 > 0.8f * 0.8f || l2 < 1e-6f) continue;
        float l = std::sqrt(l2);
        Vec2 push = d / l * ((0.8f - l) * std::min(1.0f, dt * 6.0f) * 0.5f);
        phys::moveCircle(world_, a.pos, push, 0.3f);
        if (!b.stationary) phys::moveCircle(world_, b.pos, push * -1.0f, 0.3f);
      }
    }
  }
  // ---- birds: a flock circling the neighbourhood by day, pigeons on the pavement that scatter when approached
  {
    bool day = day_.night < 0.5f && rain_ < 0.4f;
    if (birds_.empty() && day && !player_.indoors) {
      for (int i = 0; i < 7; ++i) {
        Bird b;
        float a = hash01u(world_.seed + i * 17u) * kTau;
        b.orbit = 22.0f + 22.0f * hash01u(i * 31u + 3u);
        b.alt = 14.0f + 9.0f * hash01u(i * 13u + 5u);
        b.speed = 5.5f + 2.5f * hash01u(i * 7u + 11u);
        b.phase = a;
        b.t = a;
        b.flying = true;
        b.pos = {player_.pos.x + std::cos(a) * b.orbit, b.alt, player_.pos.y + std::sin(a) * b.orbit};
        birds_.push_back(b);
      }
      for (int i = 0; i < 6 && !world_.pickupSpots.empty(); ++i) {
        Bird b;
        const Vec3& s = world_.pickupSpots[(size_t)(hash01u(i * 59u + world_.seed) * world_.pickupSpots.size()) % world_.pickupSpots.size()];
        b.home = {s.x + hash01u(i * 3u + 1u) * 2.0f - 1.0f, s.z + hash01u(i * 5u + 2u) * 2.0f - 1.0f};
        b.pos = {b.home.x, world_.heightAt(b.home.x, b.home.y) + 0.12f, b.home.y};
        b.flying = false;
        b.phase = hash01u(i * 11u) * kTau;
        birds_.push_back(b);
      }
    }
    for (Bird& b : birds_) {
      b.phase += dt * (b.flying ? 14.0f : 0.0f);
      if (b.flying && b.alt > 5.0f && b.orbit > 0.0f && b.speed > 0.0f && b.home.lengthSq() == 0.0f) {
        // orbit around the player at its own altitude (the whole flock drifts with the player)
        b.t += dt * b.speed / std::max(10.0f, b.orbit);
        Vec2 c = player_.pos + Vec2{std::cos(b.t * 0.37f) * 12.0f, std::sin(b.t * 0.29f) * 12.0f};
        Vec3 np{c.x + std::cos(b.t) * b.orbit, b.alt + std::sin(b.t * 2.1f) * 1.2f, c.y + std::sin(b.t) * b.orbit};
        Vec3 dv = np - b.pos;
        b.dir = Vec2{dv.x, dv.z}.lengthSq() > 1e-5f ? Vec2{dv.x, dv.z}.normalized() : b.dir;
        b.pos = np;
      } else if (!b.flying) {
        // pigeon: stays put until someone gets close, then takes off
        if ((Vec2{b.pos.x, b.pos.z} - player_.pos).length() < 4.0f) { b.flying = true; b.speed = 7.0f; b.alt = 0.5f; b.dir = (Vec2{b.pos.x, b.pos.z} - player_.pos).normalized(); b.t = 0; }
      } else {
        // a scattering pigeon climbs away and is retired
        b.t += dt;
        b.pos.x += b.dir.x * b.speed * dt; b.pos.z += b.dir.y * b.speed * dt; b.pos.y += 4.0f * dt;
        if (b.t > 5.0f) b.speed = -1.0f;
      }
    }
    birds_.erase(std::remove_if(birds_.begin(), birds_.end(), [](const Bird& b) { return b.speed < 0.0f; }), birds_.end());
    if (!day && !birds_.empty() && day_.night > 0.7f) birds_.clear();
  }
  // ---- street events: a crash somewhere nearby every few minutes (daytime, quiet moments)
  if (cityEvent_.active) {
    cityEvent_.t += dt;
    if (cityEvent_.t > cityEvent_.life || (cityEvent_.pos.x - player_.pos.x) * (cityEvent_.pos.x - player_.pos.x) + (cityEvent_.pos.z - player_.pos.y) * (cityEvent_.pos.z - player_.pos.y) > 260.0f * 260.0f)
      endCityEvent();
  } else {
    eventCooldown_ -= dt;
    if (eventCooldown_ <= 0.0f && !player_.indoors && wanted_ == 0 && player_.vehicle < 0 && rain_ < 0.5f) {
      startAccident();
      eventCooldown_ = 260.0f + (float)rng_.range(0.0f, 200.0f);
    }
  }
}

// A two-car crash: wrecked cars with smoke on a street 70-140 m away, curious pedestrians and a marker on the map.
void Game::startAccident() {
  size_t before = vehicles_.size();
  int made = 0;
  std::vector<int> ids;
  for (int i = 0; i < 2; ++i) {
    size_t n0 = vehicles_.size();
    if (!spawnTrafficCar(true)) continue;
    // spawnTrafficCar may have reused a recycled slot: find the newest live traffic car
    int idx = -1;
    for (size_t k = vehicles_.size(); k-- > 0;) if (vehicles_[k].traffic && !vehicles_[k].despawn && vehicles_[k].route.size() > 0 && vehicles_[k].speed >= 0.0f) { idx = (int)k; break; }
    (void)n0;
    if (idx < 0) continue;
    ids.push_back(idx);
    ++made;
  }
  (void)before;
  if (made < 1) return;
  Vehicle& a = vehicles_[ids[0]];
  a.traffic = false; a.wrecked = true; a.health = 6.0f; a.speed = 0; a.vel = {}; a.engineOn = true; a.route.clear();
  Vec2 centre = a.pos;
  if (ids.size() > 1) {
    Vehicle& b = vehicles_[ids[1]];
    b.traffic = false; b.wrecked = true; b.health = 9.0f; b.speed = 0; b.vel = {}; b.engineOn = true; b.route.clear();
    b.pos = a.pos + fwd2(a.yaw + 1.2f) * 3.2f;
    b.yaw = a.yaw + 0.7f;
    centre = (a.pos + b.pos) * 0.5f;
  }
  cityEvent_ = CityEvent();
  cityEvent_.active = true;
  cityEvent_.kind = 0;
  cityEvent_.pos = {centre.x, 0, centre.y};
  cityEvent_.vehicles = ids;
  // onlookers keep a respectful distance and comment
  for (int i = 0; i < 3; ++i) {
    float ang = rng_.range(0.0f, kTau), r = rng_.range(5.0f, 8.0f);
    Vec2 p = centre + Vec2{std::cos(ang), std::sin(ang)} * r;
    float yawTo = yawFromDir(centre - p);
    int id = debugSpawnNpc(i == 0 ? "homem_polo" : (i == 1 ? "mulher_vestido" : "vizinho"), p, yawTo, false);
    cityEvent_.npcs.push_back(id);
    for (Npc& n : npcs_) if (n.id == id) { n.bubble = i == 0 ? "Meu Deus, que batida!" : (i == 1 ? "Alguém chama a ambulância!" : "Eita! Tá todo mundo bem?"); n.bubbleTimer = 6.0f; n.stateTimer = 1e6f; }
  }
  std::string where = locationName(centre, false);
  float d = (centre - player_.pos).length();
  toast("Acidente na " + where + "|a " + std::to_string((int)d) + " m de você", "car", rgba(0.93f, 0.72f, 0.40f));
}

void Game::endCityEvent() {
  for (int vi : cityEvent_.vehicles) if (vi >= 0 && vi < (int)vehicles_.size() && vehicles_[vi].wrecked && vehicles_[vi].occupant < 0) vehicles_[vi].despawn = true;
  for (int id : cityEvent_.npcs) for (Npc& n : npcs_) if (n.id == id && (n.pos - player_.pos).length() > 40.0f) n.despawn = true;
  cityEvent_ = CityEvent();
}

// ------------------------------------------------------------------------------------------------ birds (billboards)
void Game::emitBirds() {
  UvRect dot = assets_.icon("dot");
  if (!dot.valid || birds_.empty()) return;
  SpriteDef sd;
  sd.tex = assets_.iconsTex; sd.u0 = dot.u0; sd.v0 = dot.v0; sd.u1 = dot.u1; sd.v1 = dot.v1; sd.pivX = 0.5f; sd.pivY = 0.5f; sd.valid = true;
  for (const Bird& b : birds_) {
    if (!cam_.frustum().intersectsSphere(b.pos, 1.0f)) continue;
    Vec2 side{-b.dir.y, b.dir.x};
    if (b.flying) {
      float flap = std::sin(b.phase) * 0.28f;
      sd.wm = sd.hm = 0.22f;
      for (float s : {-1.0f, 1.0f}) {
        Vec3 wp{b.pos.x + side.x * 0.28f * s, b.pos.y + flap * (s * s) + 0.05f, b.pos.z + side.y * 0.28f * s};
        addSprite(&sd, wp, 1.0f, 0.9f, false, 0xFF2A2E34u, false, false);
      }
      sd.wm = sd.hm = 0.2f;
      addSprite(&sd, b.pos, 1.0f, 0.95f, false, 0xFF1C2024u, false, false);
    } else {
      sd.wm = sd.hm = 0.26f;
      addSprite(&sd, b.pos, 1.0f, 0.95f, false, 0xFF7A808Au, false, false);
      sd.wm = sd.hm = 0.13f;
      addSprite(&sd, b.pos + Vec3{0.0f, 0.12f, 0.0f}, 1.0f, 0.95f, false, 0xFF5A606Au, false, false);
    }
  }
}

}  // namespace gtabr

namespace gtabr {
// People caught with cash drop a few notes when they go down; the player picks them up by walking over them.
void Game::dropCash(const Npc& n) {
  int cents = 300 + (int)(rng_.uni() * 2200.0f);
  if (n.role != 0) cents += 1500;   // shopkeepers carry more
  cents = cents / 10 * 10;
  cash_.push_back({{n.pos.x, world_.heightAt(n.pos.x, n.pos.y) + 0.12f, n.pos.y}, cents, 0.0f});
}

void Game::updateCash(float dt) {
  for (CashDrop& c : cash_) {
    c.t += dt;
    if (c.t > 0.5f && !player_.indoors && !player_.dead && (Vec2{c.pos.x, c.pos.z} - player_.pos).length() < 1.5f) {
      moneyCents_ += c.cents;
      toast("Dinheiro|+" + fmtMoney(c.cents), "cash", theme::kOk);
      audio_.play("cash", {c.pos.x, c.pos.y, c.pos.z}, 0.7f);
      c.t = 1e9f;
    }
  }
  cash_.erase(std::remove_if(cash_.begin(), cash_.end(), [](const CashDrop& c) { return c.t > 90.0f; }), cash_.end());
}

void Game::emitCash() {
  UvRect dot = assets_.icon("dot");
  if (!dot.valid || cash_.empty()) return;
  SpriteDef sd;
  sd.tex = assets_.iconsTex; sd.u0 = dot.u0; sd.v0 = dot.v0; sd.u1 = dot.u1; sd.v1 = dot.v1; sd.pivX = 0.5f; sd.pivY = 0.5f; sd.valid = true;
  for (const CashDrop& c : cash_) {
    if (!cam_.frustum().intersectsSphere(c.pos, 1.0f)) continue;
    float bob = 0.08f * std::sin(c.t * 4.0f) + 0.1f;
    sd.wm = 0.34f; sd.hm = 0.2f;
    addSprite(&sd, c.pos + Vec3{0, bob, 0}, 1.0f, 0.95f, false, 0xFF55B87Au, false, false);
    sd.wm = 0.1f; sd.hm = 0.1f;
    float tw = 0.5f + 0.5f * std::sin(c.t * 7.0f);
    addSprite(&sd, c.pos + Vec3{0.05f, bob + 0.18f, 0}, 1.0f, 0.9f, false, tw > 0.7f ? 0xFFFFFFFFu : 0x00FFFFFFu, false, false);
  }
}
}  // namespace gtabr
