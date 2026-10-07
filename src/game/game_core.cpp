#include <algorithm>
#include <cmath>

#include "../core/fileio.h"
#include "../core/log.h"
#include "game.h"

namespace gtabr {

// ------------------------------------------------------------------------------------------------ lifecycle
bool Game::init(const Init& i) {
  cfg_ = i;
  r_ = i.renderer;
  jobs_ = i.jobs;
  fileio::setSaveDir(i.saveDir);
  assets_.startLoading(r_, jobs_);
  startWorldJob();
  cam_.init(CamMode::TopDown);
  return true;
}

void Game::shutdown() {
  if (phase_ == Phase::Playing) saveGame();
  jobs_->waitIdle();
}

void Game::startWorldJob() {
  jobs_->submit([this]() {
    buildWorld(world_);
    worldReady_ = true;
  });
}

void Game::finishLoading() {
  // GPU meshes
  for (auto& c : world_.chunks) {
    if (c.mesh.empty()) continue;
    c.handle = r_->createMesh(c.mesh.v.data(), c.mesh.v.size(), c.mesh.idx.data(), c.mesh.idx.size());
    c.bounds = c.mesh.bounds;
    c.mesh.v.clear(); c.mesh.v.shrink_to_fit(); c.mesh.idx.clear(); c.mesh.idx.shrink_to_fit();
  }
  if (!world_.marketCeiling.empty()) {
    world_.marketCeilingHandle = r_->createMesh(world_.marketCeiling.v.data(), world_.marketCeiling.v.size(), world_.marketCeiling.idx.data(),
                                                world_.marketCeiling.idx.size());
  }
  materials_ = assets_.materials;
  buildSpriteTables();
  // navigation
  {
    std::vector<RectF> blockers;
    for (const Collider& c : world_.colliders) {
      if (c.box.mn.x > 250) continue;  // interior geometry
      if (c.box.mn.y > 2.0f) continue;
      blockers.push_back({c.box.mn.x, c.box.mn.z, c.box.mx.x, c.box.mx.z});
    }
    navOutdoor_.build({-86, -86, 86, 86}, 0.5f, world_.walkable, blockers, 0.38f);
    navIndoor_.build({World::kHalf * 0 + 288, -8, 312, 8}, 0.4f, world_.interiorWalkable, world_.interiorBlockers, 0.35f);
    LOGI("Navmesh: outdoor %zu polys, indoor %zu polys", navOutdoor_.polyCount(), navIndoor_.polyCount());
  }
  // minimap texture
  {
    std::vector<uint8_t> px;
    renderMinimap(world_, px, 512, mapExtent_);
    mapTex_ = r_->createTextureRGBA(512, 512, px.data(), true, true, gfx::SamplerKind::ClampLinear);
  }
  resetEntities(true);
  bool loaded = false;
  if (!cfg_.newGame) loaded = loadGame();
  if (!loaded) LOGI("Starting a new game");
  CameraInput ci;
  ci.focus = {player_.pos.x, player_.y, player_.pos.y};
  ci.headingYaw = player_.yaw;
  cam_.snapTo(ci, world_, screenW_ / std::max(1.0f, screenH_));
  phase_ = Phase::Playing;
  fadeAlpha_ = 1.0f;
  fadeTarget_ = 0.0f;
  applySettings();
  toast("Bem-vindo ao bairro! Ache o carro e vá ao posto.", "pin");
}

void Game::resetEntities(bool fresh) {
  (void)fresh;
  vehicles_.clear();
  for (int i = 0; i < 3; ++i) {
    Vehicle v;
    v.id = i;
    v.model = i;
    v.color = 0;
    v.pos = {world_.vehicleSpawn[i].x, world_.vehicleSpawn[i].z};
    v.yaw = world_.vehicleYaw[i];
    const VehicleDef& d = vehicleDef(i);
    const float fuelFrac[3] = {0.20f, 0.55f, 0.35f};
    const float health[3] = {78.0f, 62.0f, 91.0f};
    v.fuel = d.fuelCap * fuelFrac[i];
    v.health = health[i];
    vehicles_.push_back(v);
  }
  player_ = Player();
  player_.pos = {world_.spawnPlayer.x, world_.spawnPlayer.z};
  player_.yaw = player_.targetYaw = world_.spawnYaw;
  player_.y = world_.heightAt(player_.pos.x, player_.pos.y);
  moneyCents_ = 40000;
  moneyDisplay_ = (float)moneyCents_;
  for (int& c : inventory_) c = 0;
  inventory_[1] = 1;  // a water bottle to start with
  wheel_.slots[0] = {0};
  wheel_.slots[1] = {};
  spawnNpcs();
}

void Game::teleportPlayer(Vec2 p, float yaw) {
  player_.pos = p;
  player_.yaw = player_.targetYaw = yaw;
  player_.vel = {};
  player_.y = world_.heightAt(p.x, p.y);
  player_.indoors = world_.inInterior(p.x, p.y);
}

// ------------------------------------------------------------------------------------------------ frame
void Game::frame(float dtReal, gfx::FrameData& fd) {
  dtReal = clamp(dtReal, 0.0005f, 0.1f);
  realTime_ += dtReal;
  fd_ = &fd;
  fd.clear();
  fd.materialArray = materials_;
  fd.blur = 0; fd.fade = 0; fd.dim = 0;

  // smoothed frame time for adaptive quality + FPS counter
  frameMsAvg_ += (dtReal * 1000.0f - frameMsAvg_) * 0.05f;
  fpsAccum_ += dtReal; fpsFrames_++;
  if (fpsAccum_ >= 0.5f) { fpsShown_ = fpsFrames_ / fpsAccum_; fpsAccum_ = 0; fpsFrames_ = 0; }

  InputLayout layout = makeLayout();
  InputFrame in;
  if (useScripted_) {
    in = scripted_;
    scripted_.interactPressed = scripted_.enterExitPressed = scripted_.cameraPressed = scripted_.pausePressed = false;
    scripted_.wheelPressed = scripted_.wheelReleased = false;
    scripted_.look = {}; scripted_.zoom = 0; scripted_.ui.clear();
  } else {
    in = input_.poll(layout);
  }

  visualInput_ = in;
  if (phase_ == Phase::Loading) {
    loadingAnim_ += dtReal;
    bool assetsDone = assets_.pump();
    if (assets_.failed()) LOGE("asset load failure");
    if (assetsDone && worldReady_) finishLoading();
    if (phase_ == Phase::Loading) {
      ui_.begin(&fd, &assets_, screenW_, screenH_, uiScale());
      drawLoading(dtReal);
      ui_.end();
      // before fonts exist we still need a clear frame; defaults in FrameData are fine
      setupGlobals(fd);
      return;
    }
  }
  updatePlaying(dtReal, in);
  setupGlobals(fd);
  buildScene(fd);
  buildUi(fd, dtReal);
}

void Game::updatePlaying(float dtReal, const InputFrame& in) {
  // ---- fade transitions
  {
    float rate = 4.5f;
    if (fadeAlpha_ < fadeTarget_) fadeAlpha_ = std::min(fadeTarget_, fadeAlpha_ + rate * dtReal);
    else if (fadeAlpha_ > fadeTarget_) fadeAlpha_ = std::max(fadeTarget_, fadeAlpha_ - rate * dtReal);
    if (fadeTarget_ >= 1.0f && fadeAlpha_ >= 0.995f && fadeThen_) {
      auto fn = std::move(fadeThen_);
      fadeThen_ = nullptr;
      fn();
      fadeTarget_ = 0.0f;
    }
  }
  // ---- menus take over the whole input
  if (menu_ != MenuState::None) {
    handleUiPointers(in);
    timeScale_ += (0.0f - timeScale_) * expDecay(20.0f, dtReal);
    // camera keeps the scene alive but frozen
    return;
  }
  // ---- radial wheel
  if (in.wheelPressed && !panel_.open && fadeAlpha_ < 0.3f && !player_.entering && !player_.exiting) {
    wheel_.open = true;
    wheel_.category = 1;
    wheel_.slots[0].clear();
    wheel_.slots[0].push_back(0);
    wheel_.slots[1].clear();
    for (int i = 1; i < kItemCount; ++i)
      if (inventory_[i] > 0) wheel_.slots[1].push_back(i);
    wheel_.hovered = -1;
  }
  if (wheel_.open) {
    wheel_.finger = in.wheelPos;
    Vec2 c{screenW_ * 0.5f, screenH_ * 0.5f};
    Vec2 d = wheel_.finger - c;
    float R = std::min(screenW_, screenH_) * 0.40f;
    float len = d.length();
    wheel_.hoveredCategory = -1;
    int prevHover = wheel_.hovered;
    if (len > R * 0.12f && len < R * 0.44f) {
      // inner ring: two halves select the category (top = weapons, bottom = items)
      wheel_.hoveredCategory = d.y < 0 ? 0 : 1;
      if (wheel_.category != wheel_.hoveredCategory) { wheel_.category = wheel_.hoveredCategory; wheel_.hovered = -1; }
    } else if (len >= R * 0.5f) {
      float ang = std::atan2(d.x, -d.y);
      if (ang < 0) ang += kTau;
      int slot = (int)std::floor((ang + kTau / 16.0f) / (kTau / 8.0f)) % 8;
      wheel_.hovered = slot < (int)wheel_.slots[wheel_.category].size() ? slot : -1;
    } else if (len < R * 0.12f) {
      wheel_.hovered = -1;
    }
    (void)prevHover;
    if (in.wheelReleased || (!in.wheelHeld && !useScripted_)) {
      if (wheel_.hovered >= 0 && wheel_.hovered < (int)wheel_.slots[wheel_.category].size()) {
        int item = wheel_.slots[wheel_.category][wheel_.hovered];
        if (itemDef(item).category == ItemCategory::Weapon) {
          player_.weapon = item;
          toast(std::string("Equipado: ") + itemDef(item).name, "fist");
        } else useItem(item);
      }
      wheel_.open = false;
    }
  }
  float wheelTarget = wheel_.open ? 1.0f : 0.0f;
  wheel_.anim += (wheelTarget - wheel_.anim) * expDecay(wheel_.open ? 14.0f : 11.0f, dtReal);
  if (wheel_.anim < 0.002f && !wheel_.open) wheel_.anim = 0;
  blur_ = wheel_.anim;

  // ---- panel input (taps on options)
  if (panel_.open) handleUiPointers(in);

  // ---- global buttons
  if (in.pausePressed && !panel_.open) { menu_ = MenuState::Pause; return; }
  if (in.cameraPressed && !wheel_.open) toggleCamera();
  if (!panel_.open && !wheel_.open && fadeAlpha_ < 0.5f) {
    if (in.enterExitPressed) tryEnterExit();
    if (in.interactPressed && focusValid_) activateInteractable(focus_);
  }

  // slow motion while the wheel is open
  float targetScale = wheel_.open ? 0.15f : 1.0f;
  timeScale_ += (targetScale - timeScale_) * expDecay(10.0f, dtReal);
  float dt = dtReal * timeScale_;
  time_ += dt;

  InputFrame gameIn = in;
  if (panel_.open || wheel_.open || fadeAlpha_ > 0.6f) { gameIn.move = {}; gameIn.runHeld = false; }

  updatePlayer(dt, gameIn);
  updateVehicles(dt, gameIn);
  updateNpcs(dt);
  updateParticles(dt);

  // fuelling
  if (fueling_.active) {
    Vehicle& v = vehicles_[fueling_.vehicle];
    float step = std::min(fueling_.litersLeft, fueling_.rate * dt);
    v.fuel = std::min(vehicleDef(v.model).fuelCap, v.fuel + step);
    fueling_.litersLeft -= step;
    if (fueling_.litersLeft <= 1e-4f) {
      fueling_.active = false;
      toast("Tanque abastecido: +" + fmtFloat(fueling_.total, 1) + " L", "fuel");
    }
  }

  // interaction focus
  collectInteractables();

  // camera
  {
    CameraInput ci;
    if (player_.vehicle >= 0) {
      const Vehicle& v = vehicles_[player_.vehicle];
      ci.focus = {v.pos.x, world_.heightAt(v.pos.x, v.pos.y), v.pos.y};
      ci.headingYaw = v.yaw; ci.velocity = v.vel; ci.speed = v.speed; ci.driving = true;
    } else {
      ci.focus = {player_.pos.x, player_.y, player_.pos.y};
      ci.headingYaw = player_.yaw; ci.velocity = player_.vel; ci.speed = player_.speed;
    }
    ci.indoors = player_.indoors;
    ci.look = in.look; ci.zoomDelta = in.zoom; ci.userDragging = in.lookDragging;
    ci.shake = cameraShake_;
    cam_.sensitivity = settings_.sensitivity;
    cam_.invertY = settings_.invertY;
    if (!panel_.open && !wheel_.open) cam_.update(dtReal, ci, world_, screenW_ / std::max(1.0f, screenH_));
    else { CameraInput frozen = ci; frozen.look = {}; frozen.zoomDelta = 0; cam_.update(dtReal, frozen, world_, screenW_ / std::max(1.0f, screenH_)); }
    cameraShake_ = std::max(0.0f, cameraShake_ - dtReal * 2.5f);
  }
  indoorBlend_ += ((player_.indoors ? 1.0f : 0.0f) - indoorBlend_) * expDecay(6.0f, dtReal);

  // timers for HUD
  if (std::fabs(player_.health - lastHealth_) > 0.01f || player_.health < 40) healthShow_ = 3.5f;
  lastHealth_ = player_.health;
  healthShow_ = std::max(0.0f, healthShow_ - dtReal);
  if (player_.stamina < 99.5f || player_.running) staminaShow_ = 2.5f;
  else staminaShow_ = std::max(0.0f, staminaShow_ - dtReal);
  moneyShow_ = std::max(0.0f, moneyShow_ - dtReal);
  moneyDisplay_ += ((float)moneyCents_ - moneyDisplay_) * expDecay(7.0f, dtReal);
  if (std::fabs(moneyDisplay_ - moneyCents_) < 1.0f) moneyDisplay_ = (float)moneyCents_;
  neighbourCooldown_ = std::max(0.0f, neighbourCooldown_ - dtReal);

  autosave_ += dtReal;
  if (autosave_ > 25.0f && !fueling_.active && fadeAlpha_ < 0.05f) { autosave_ = 0; saveGame(); }
  updateAdaptiveQuality(dtReal);
}

void Game::updateAdaptiveQuality(float dt) {
  adaptTimer_ += dt;
  if (adaptTimer_ < 3.0f || settings_.quality != 0) return;
  adaptTimer_ = 0;
  float s = r_->renderScale();
  if (frameMsAvg_ > 36.0f && s > 0.61f) r_->setRenderScale(s - 0.1f);
  else if (frameMsAvg_ < 20.0f && s < 0.99f) r_->setRenderScale(std::min(1.0f, s + 0.1f));
}

void Game::applySettings() {
  switch (settings_.quality) {
    case 1: r_->setRenderScale(0.65f); break;
    case 2: r_->setRenderScale(0.85f); break;
    case 3: r_->setRenderScale(1.0f); break;
    default: break;
  }
  r_->setShadowsEnabled(settings_.shadows && settings_.quality != 1);
  cam_.sensitivity = settings_.sensitivity;
  cam_.invertY = settings_.invertY;
}

void Game::toast(const std::string& s, const char* icon, uint32_t color) {
  Toast t;
  t.text = s; t.icon = icon; t.color = color; t.t = 0;
  toasts_.push_back(t);
  if (toasts_.size() > 4) toasts_.pop_front();
}

void Game::toggleCamera() {
  cam_.toggle();
  toast(cam_.mode() == CamMode::TopDown ? "Câmera: Top Down" : "Câmera: Terceira Pessoa", "camera");
}

// ------------------------------------------------------------------------------------------------ player
static float distToObb(const Vec2& p, const phys::OBB& o) {
  Vec2 ax = right2(o.yaw), ay = fwd2(o.yaw);
  Vec2 d = p - o.c;
  float lx = std::fabs(d.dot(ax)) - o.half.x, ly = std::fabs(d.dot(ay)) - o.half.y;
  lx = std::max(lx, 0.0f); ly = std::max(ly, 0.0f);
  return std::sqrt(lx * lx + ly * ly);
}

int Game::nearestVehicleTo(Vec2 p, float maxDist, bool) const {
  int best = -1;
  float bd = maxDist;
  for (size_t i = 0; i < vehicles_.size(); ++i) {
    float d = distToObb(p, vehicleObb(vehicles_[i]));
    if (d < bd) { bd = d; best = (int)i; }
  }
  return best;
}

void Game::tryEnterExit() {
  if (player_.entering || player_.exiting) return;
  if (player_.vehicle >= 0) {
    Vehicle& v = vehicles_[player_.vehicle];
    if (std::fabs(v.speed) > 6.0f) { toast("Pare o veículo para sair", "car"); return; }
    if (fueling_.active) fueling_.active = false;
    player_.exiting = true;
    player_.transition = 0;
    player_.transitionVehicle = player_.vehicle;
    return;
  }
  if (player_.indoors) return;
  int vi = nearestVehicleTo(player_.pos, 2.4f);
  if (vi < 0) { toast("Nenhum veículo por perto", "car"); return; }
  if (vehicles_[vi].occupant >= 0) return;
  player_.entering = true;
  player_.transition = 0;
  player_.transitionVehicle = vi;
}

void Game::updatePlayer(float dt, const InputFrame& in) {
  Player& p = player_;
  p.hurtTimer = std::max(0.0f, p.hurtTimer - dt);
  // ---- enter / exit animation
  if (p.entering || p.exiting) {
    p.transition += dt / 0.42f;
    Vehicle& v = vehicles_[p.transitionVehicle];
    const VehicleDef& d = vehicleDef(v.model);
    Vec2 door = v.pos - right2(v.yaw) * (d.width * 0.5f + 0.55f) + fwd2(v.yaw) * 0.3f;
    if (p.entering) {
      p.pos = lerp(p.pos, door, expDecay(10.0f, dt));
      if (p.transition >= 1.0f) {
        p.entering = false;
        p.vehicle = p.transitionVehicle;
        v.occupant = 0;
        v.engineOn = true;
        p.transition = 0;
        toast(std::string("Dirigindo: ") + d.name, "car");
        if (v.fuel <= 0.0f) toast("Sem combustível!", "fuel", rgba(1.0f, 0.5f, 0.4f));
      }
    } else {
      if (p.transition >= 1.0f) {
        p.exiting = false;
        p.transition = 0;
        v.occupant = -1;
        v.engineOn = false;
        p.vehicle = -1;
      } else if (p.transition < 0.05f) {
        // choose a free exit spot: driver side, passenger side, behind
        Vec2 cand[3] = {v.pos - right2(v.yaw) * (d.width * 0.5f + 0.75f), v.pos + right2(v.yaw) * (d.width * 0.5f + 0.75f),
                        v.pos - fwd2(v.yaw) * (d.length * 0.5f + 0.9f)};
        Vec2 best = cand[0];
        for (Vec2 c : cand) {
          Vec2 t = c;
          phys::depenetrateCircle(world_, t, 0.34f);
          if ((t - c).length() < 0.05f) { best = c; break; }
          best = t;
        }
        p.pos = best;
        p.yaw = p.targetYaw = wrapAngle(v.yaw + kPi * 0.5f);
        p.vehicle = -1;   // visible on foot again, still blocked from input until done
        v.occupant = -1;
      }
    }
    return;
  }
  if (p.vehicle >= 0) {
    Vehicle& v = vehicles_[p.vehicle];
    p.pos = v.pos;
    p.y = world_.heightAt(v.pos.x, v.pos.y);
    p.speed = 0;
    p.running = false;
    p.stamina = std::min(100.0f, p.stamina + 10.0f * dt);
    p.indoors = false;
    return;
  }

  // ---- walking
  Vec2 mv = in.move;
  float mag = std::min(1.0f, mv.length());
  Vec2 camF2 = {std::sin(cam_.yaw()), -std::cos(cam_.yaw())};
  Vec2 camR2 = {std::cos(cam_.yaw()), std::sin(cam_.yaw())};
  Vec2 dir = camF2 * mv.y + camR2 * mv.x;
  bool wantRun = in.runHeld && mag > 0.2f;
  bool canRun = (p.stamina > 6.0f || p.runBoost > 0.0f) && p.staminaCooldown <= 0.0f;
  p.running = wantRun && canRun;
  float maxSpeed = p.running ? 6.4f : 3.1f;
  Vec2 desired = mag > 0.01f ? dir.normalized() * (maxSpeed * (p.running ? 1.0f : std::max(0.45f, mag))) : Vec2{0, 0};
  float accel = mag > 0.01f ? 16.0f : 20.0f;
  p.vel += (desired - p.vel) * expDecay(accel, dt);
  if (p.running) {
    if (p.runBoost > 0.0f) p.runBoost -= dt;
    else p.stamina = std::max(0.0f, p.stamina - 21.0f * dt);
    if (p.stamina <= 0.0f && p.runBoost <= 0.0f) { p.staminaCooldown = 1.6f; p.running = false; }
  } else {
    p.staminaCooldown = std::max(0.0f, p.staminaCooldown - dt);
    if (p.runBoost > 0.0f) p.runBoost -= dt;
    p.stamina = std::min(100.0f, p.stamina + (p.staminaCooldown > 0 ? 5.0f : 15.0f) * dt);
  }
  Vec2 before = p.pos;
  phys::moveCircle(world_, p.pos, p.vel * dt, 0.32f);
  // vehicles push the player
  for (const Vehicle& v : vehicles_) {
    phys::Hit h = phys::circleVsObb(p.pos, 0.32f, vehicleObb(v));
    if (h.hit) {
      p.pos += h.normal * h.depth;
      float closing = -(v.vel - p.vel).dot(h.normal);
      if (std::fabs(v.speed) > 2.5f && closing > 2.0f && p.hurtTimer <= 0.0f) {
        p.health = std::max(1.0f, p.health - closing * 3.0f);
        p.hurtTimer = 1.0f;
        p.vel += h.normal * 4.0f;
        cameraShake_ = std::max(cameraShake_, 0.5f);
        toast("Ai! Cuidado com os carros", "heart", rgba(1.0f, 0.55f, 0.5f));
      }
    }
  }
  // soft push from pedestrians
  for (const Npc& n : npcs_) {
    if (n.interior != p.indoors) continue;
    Vec2 d = p.pos - n.pos;
    float l = d.length();
    if (l < 0.55f && l > 1e-4f) p.pos += d / l * (0.55f - l) * 0.5f;
  }
  if (!p.indoors) {
    p.pos.x = clamp(p.pos.x, -World::kHalf + 0.7f, World::kHalf - 0.7f);
    p.pos.y = clamp(p.pos.y, -World::kHalf + 0.7f, World::kHalf - 0.7f);
  }
  Vec2 actual = (p.pos - before) / std::max(dt, 1e-4f);
  p.speed = actual.length();
  if (p.speed > 0.35f) {
    float want = yawFromDir(p.vel.length() > 0.2f ? p.vel : actual);
    p.targetYaw = want;
  }
  p.yaw = lerpAngle(p.yaw, p.targetYaw, expDecay(13.0f, dt));
  float stride = p.running ? 2.5f : 1.55f;
  if (p.speed > 0.25f) p.animTime += p.speed * dt / stride;
  float hy = world_.heightAt(p.pos.x, p.pos.y);
  p.y += (hy - p.y) * expDecay(18.0f, dt);
  p.indoors = world_.inInterior(p.pos.x, p.pos.y);
  p.health = std::min(100.0f, p.health + 0.4f * dt);   // slow natural recovery
}

// ------------------------------------------------------------------------------------------------ vehicles
void Game::updateVehicles(float dt, const InputFrame& in) {
  // sub-step for stability at low frame rates
  int steps = std::max(1, (int)std::ceil(dt / (1.0f / 60.0f)));
  float h = dt / steps;
  for (int s = 0; s < steps; ++s) {
    for (Vehicle& v : vehicles_) {
      VehicleInput vi;
      bool driven = (player_.vehicle == v.id && !player_.exiting);
      if (driven) {
        float t = in.move.y, st = in.move.x;
        if (std::fabs(t) < 0.12f) t = 0;
        vi.throttle = clamp(t, -1.0f, 1.0f);
        vi.steer = clamp(st * 1.15f, -1.0f, 1.0f);
        vi.handbrake = in.runHeld;
        if (panel_.open || fueling_.active) { vi.throttle = 0; vi.handbrake = true; }
      } else {
        vi.handbrake = true;   // parked: brakes on
      }
      if (v.occupant < 0 && std::fabs(v.speed) < 0.15f && v.vel.length() < 0.15f) { v.vel = {}; v.speed = 0; v.yawRate = 0; continue; }
      float impact = stepVehicle(v, vi, h, world_, vehicles_);
      if (impact > 2.0f && driven) {
        cameraShake_ = std::max(cameraShake_, clamp(impact / 12.0f, 0.1f, 1.0f));
        if (impact > 7.0f) {
          player_.health = std::max(1.0f, player_.health - (impact - 7.0f) * 1.6f);
          toast("Batida forte!", "car", rgba(1.0f, 0.55f, 0.45f));
        }
      }
      // pedestrians
      if (std::fabs(v.speed) > 1.5f) {
        phys::OBB o = vehicleObb(v);
        for (Npc& n : npcs_) {
          if (n.interior) continue;
          phys::Hit hh = phys::circleVsObb(n.pos, 0.3f, o);
          if (!hh.hit) continue;
          n.pos += hh.normal * hh.depth;
          if (n.state != NpcState::Stunned) {
            n.state = NpcState::Stunned;
            n.stateTimer = 2.2f;
            n.bubble = "Ei!!";
            n.bubbleTimer = 1.6f;
            n.path.clear();
            if (driven) cameraShake_ = std::max(cameraShake_, 0.3f);
          }
        }
      }
    }
  }
  // smoke for damaged vehicles
  for (Vehicle& v : vehicles_) {
    if (v.health < 30.0f && v.engineOn) {
      v.smokeTimer += 0;
      if (rng_.chance(dt * 18.0f)) {
        int idx;
        Particle* p = particles_.acquire(&idx);
        if (p) {
          Vec2 fwd = fwd2(v.yaw);
          Vec2 pos = v.pos + fwd * (vehicleDef(v.model).length * 0.35f);
          p->pos = {pos.x, 0.9f, pos.y};
          p->vel = {rng_.range(-0.3f, 0.3f), rng_.range(0.8f, 1.5f), rng_.range(-0.3f, 0.3f)};
          p->life = p->maxLife = rng_.range(0.9f, 1.6f);
          p->size = rng_.range(0.5f, 0.9f);
          p->color = rgba(0.25f, 0.25f, 0.26f, 0.55f);
        }
      }
    }
  }
}

void Game::updateParticles(float dt) {
  particles_.forEach([&](Particle& p, int idx) {
    p.life -= dt;
    if (p.life <= 0) { particles_.release(idx); return; }
    p.pos += p.vel * dt;
    p.size += dt * 0.7f;
  });
}

// ------------------------------------------------------------------------------------------------ interactions
int Game::pumpNearVehicle(const Vehicle& v) const {
  int best = -1;
  float bd = 5.6f;
  for (const PumpDef& p : world_.pumps) {
    float d = (Vec2{p.pos.x, p.pos.z} - v.pos).length();
    if (d < bd) { bd = d; best = p.id; }
  }
  return best;
}

void Game::collectInteractables() {
  nearby_.clear();
  Vec2 pp = player_.pos;
  bool driving = player_.vehicle >= 0;
  if (player_.entering || player_.exiting) { focusValid_ = false; return; }
  auto add = [&](Interactable it) {
    it.dist = (Vec2{it.pos.x, it.pos.z} - pp).length();
    if (it.dist <= it.radius) nearby_.push_back(std::move(it));
  };
  if (!driving) {
    for (size_t i = 0; i < vehicles_.size(); ++i) {
      const Vehicle& v = vehicles_[i];
      if (v.occupant >= 0 || player_.indoors) continue;
      float d = distToObb(pp, vehicleObb(v));
      if (d < 2.4f) {
        Interactable it;
        it.kind = IKind::Vehicle; it.id = (int)i; it.pos = {v.pos.x, 0, v.pos.y}; it.radius = 99; it.label = "Entrar"; it.sub = vehicleDef(v.model).name; it.icon = "car";
        it.dist = d + 0.8f;   // slightly lower priority than doors / people
        nearby_.push_back(it);
      }
    }
    for (size_t i = 0; i < npcs_.size(); ++i) {
      const Npc& n = npcs_[i];
      if (n.interior != player_.indoors) continue;
      Interactable it;
      it.kind = IKind::Npc; it.id = (int)i; it.pos = {n.pos.x, 0, n.pos.y}; it.radius = n.role == 4 ? 3.6f : 2.3f;
      it.label = n.role == 1 || n.role == 4 || n.role == 2 ? "Falar" : "Conversar";
      switch (n.role) {
        case 1: it.sub = "Frentista"; break;
        case 2: it.sub = "Mecânico"; break;
        case 3: it.sub = "Seu Manoel"; break;
        case 4: it.sub = "Atendente"; break;
        default: it.sub = "Morador"; break;
      }
      it.icon = "chat";
      add(it);
    }
    for (const DoorDef& d : world_.doors) {
      if (d.toInterior == player_.indoors) continue;   // exterior doors are only usable outside, exit doors inside
      Interactable it;
      it.kind = IKind::Door; it.id = d.id; it.pos = d.pos; it.radius = d.radius; it.label = d.toInterior ? "Entrar" : "Sair"; it.sub = d.toInterior ? "Mercado do Zé" : "Voltar à rua"; it.icon = "door";
      add(it);
    }
    if (player_.indoors)
      for (const ProductPoint& pr : world_.products) {
        const ItemDef& d = itemDef(pr.item);
        Interactable it;
        it.kind = IKind::Product; it.id = pr.id; it.pos = pr.pos; it.radius = 1.5f; it.label = "Comprar";
        it.sub = std::string(d.name) + " • " + fmtMoney(d.priceCents); it.icon = "cart"; it.art = d.art ? d.art : "";
        it.enabled = moneyCents_ >= d.priceCents;
        add(it);
      }
  }
  // fuel pumps: any vehicle parked at a pump, reachable from the car seat or from the pavement
  if (!player_.indoors) {
    for (const PumpDef& pd : world_.pumps) {
      Vec2 pv{pd.pos.x, pd.pos.z};
      int vi = -1;
      for (size_t i = 0; i < vehicles_.size(); ++i)
        if ((vehicles_[i].pos - pv).length() < 5.6f) { vi = (int)i; break; }
      if (vi < 0) continue;
      bool ok = driving ? (player_.vehicle == vi) : ((pp - pv).length() < 3.6f);
      if (!ok) continue;
      Interactable it;
      it.kind = IKind::FuelPump; it.id = pd.id; it.pos = {pv.x, 0, pv.y}; it.radius = 99; it.label = "Abastecer";
      it.sub = std::string("Bomba ") + std::to_string(pd.id + 1) + " • Gasolina"; it.icon = "fuel";
      it.dist = (pp - pv).length() * 0.5f;
      nearby_.push_back(it);
    }
    // workshop zone: for vehicles driven into the bay
    const RectF& bay = world_.serviceBay;
    RectF zone = bay.inflated(2.0f);
    bool inZone = zone.contains(pp.x, pp.y);
    if (inZone) {
      Interactable it;
      it.kind = IKind::Workshop; it.id = 0; it.pos = {bay.cx(), 0, bay.cz()}; it.radius = 99; it.label = "Oficina"; it.sub = "Reparo do veículo"; it.icon = "wrench";
      it.dist = 4.0f;
      nearby_.push_back(it);
    }
  }
  focusValid_ = false;
  float best = 1e9f;
  for (const Interactable& it : nearby_) {
    float score = it.dist;
    if (it.kind == IKind::Npc) score -= 0.4f;
    if (score < best) { best = score; focus_ = it; focusValid_ = true; }
  }
}

void Game::activateInteractable(const Interactable& it) {
  switch (it.kind) {
    case IKind::Npc: openNpcPanel(it.id); break;
    case IKind::Vehicle: tryEnterExit(); break;
    case IKind::Door: {
      const DoorDef* d = nullptr;
      for (const DoorDef& x : world_.doors) if (x.id == it.id) d = &x;
      if (!d) break;
      DoorDef door = *d;
      fadeTarget_ = 1.0f;
      fadeThen_ = [this, door]() {
        teleportPlayer({door.arrive.x, door.arrive.z}, door.arriveYaw);
        CameraInput ci;
        ci.focus = {player_.pos.x, player_.y, player_.pos.y};
        ci.headingYaw = door.arriveYaw;
        ci.indoors = player_.indoors;
        cam_.snapTo(ci, world_, screenW_ / std::max(1.0f, screenH_));
        toast(door.toInterior ? "Mercado do Zé" : "Na rua", door.toInterior ? "cart" : "pin");
      };
      break;
    }
    case IKind::Product: buyItem(world_.products[it.id].item, true); break;
    case IKind::FuelPump: openFuelPanel(it.id); break;
    case IKind::Workshop: openWorkshopPanel(); break;
  }
}

}  // namespace gtabr
