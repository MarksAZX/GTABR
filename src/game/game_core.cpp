#include <algorithm>
#include <cmath>
#include <chrono>

#include "../core/fileio.h"
#include "../core/log.h"
#include "game.h"

namespace gtabr {

// ------------------------------------------------------------------------------------------------ lifecycle
bool Game::init(const Init& i) {
  cfg_ = i;
  if (i.seed) worldSeed_ = i.seed;
  r_ = i.renderer;
  jobs_ = i.jobs;
  fileio::setSaveDir(i.saveDir);
  readSettings();
  if (!i.mainMenu) { activeSlot_ = 0; sessionActive_ = true; freshSlot_ = i.newGame; if (!i.newGame) readSeed(); }
  else menu_ = MenuState::Main;
  assets_.startLoading(r_, jobs_);
  queueModels();
  audio_.init();
  startWorldJob();
  cam_.init(CamMode::TopDown);
  return true;
}

void Game::shutdown() {
  if (sessionActive_ && phase_ == Phase::Playing) saveGame();
  jobs_->waitIdle();
  audio_.shutdown();
}

void Game::startWorldJob() {
  jobs_->submit([this]() {
    worldReady_ = false;
  buildWorld(world_, worldSeed_);
    worldReady_ = true;
  });
}

void Game::finishLoading() {
  // GPU meshes
  for (auto& c : world_.chunks) c.bounds = c.mesh.bounds;
  worldInstalled_ = true;
  if (!world_.interiorCeiling.empty()) {
    world_.interiorCeilingHandle = r_->createMesh(world_.interiorCeiling.v.data(), world_.interiorCeiling.v.size(), world_.interiorCeiling.idx.data(),
                                                  world_.interiorCeiling.idx.size());
  }
  materials_ = assets_.materials;
  if (initialLoading_) { finishModels(); buildWeaponMeshes(*r_, weaponMeshes_); buildDecorModels(*r_, decorModels_); }
  // weapon pickups around the neighbourhood (melee in the open, firearms in quieter corners)
  {
    // weapon pickups on spots chosen by the city generator (melee first, firearms later in the list)
    const int kinds[8] = {kWpnBat, kWpnKnife, kWpnCrowbar, kWpnBaton, kWpnPistol, kWpnRevolver, kWpnSmg, kWpnShotgun};
    const int ammo[8] = {0, 0, 0, 0, 45, 24, 90, 24};
    pickups_.clear();
    for (size_t i = 0; i < world_.pickupSpots.size() && i < 8; ++i) pickups_.push_back({kinds[i], ammo[i], world_.pickupSpots[i]});
  }
  for (Pickup& k : pickups_) {
    Vec2 p{k.pos.x, k.pos.z};
    phys::depenetrateCircle(world_, p, 0.5f);
    k.pos = {p.x, world_.heightAt(p.x, p.y), p.y};
  }
  if (initialLoading_) worldMaterial_ = r_->createWorldMaterial(assets_.materials, assets_.materialsNormal);
  buildSpriteTables();
  // navigation
  {
    std::vector<RectF> blockers;
    for (const Collider& c : world_.colliders) {
      if (c.box.mn.x > World::kInteriorX - 60.0f) continue;  // interior geometry
      if (c.box.mn.y > 2.0f) continue;
      blockers.push_back({c.box.mn.x, c.box.mn.z, c.box.mx.x, c.box.mx.z});
    }
    navOutdoor_.build(world_.playArea, 0.5f, world_.walkable, blockers, 0.38f);
    RectF ib{1e9f, 1e9f, -1e9f, -1e9f};
    for (const InteriorDef& in : world_.interiors) {
      ib.x0 = std::min(ib.x0, in.bounds.x0 - 1); ib.z0 = std::min(ib.z0, in.bounds.z0 - 1);
      ib.x1 = std::max(ib.x1, in.bounds.x1 + 1); ib.z1 = std::max(ib.z1, in.bounds.z1 + 1);
    }
    navIndoor_.build(ib, 0.4f, world_.interiorWalkable, world_.interiorBlockers, 0.35f);
    LOGI("Navmesh: outdoor %zu polys, indoor %zu polys", navOutdoor_.polyCount(), navIndoor_.polyCount());
  }
  // minimap texture
  {
    std::vector<uint8_t> px;
    mapExtent_ = world_.half;
    renderMinimap(world_, px, 512, mapExtent_);
    mapTex_ = r_->createTextureRGBA(512, 512, px.data(), true, true, gfx::SamplerKind::ClampLinear);
  }
  resetEntities(true);
  bool loaded = false;
  if (sessionActive_ && !freshSlot_) loaded = loadGame();
  if (sessionActive_ && !freshSlot_ && !loaded) {sessionActive_=false;toast("Save inválido. O arquivo foi preservado.","save");}
  else if (!loaded) LOGI("Starting a new game");
  CameraInput ci;
  ci.focus = {player_.pos.x, player_.y, player_.pos.y};
  ci.headingYaw = player_.yaw;
  cam_.snapTo(ci, world_, screenW_ / std::max(1.0f, screenH_));
  phase_ = Phase::Playing;
  fadeAlpha_ = 1.0f;
  fadeTarget_ = 0.0f;
  applySettings();
  initialLoading_ = false;
  if (sessionActive_) { menu_ = MenuState::None; if (freshSlot_) saveGame(); toast(world_.cityName + " • seed " + std::to_string(worldSeed_), "pin"); }
  else menu_ = MenuState::Main;
  updateStreaming();
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
  time_ = 0; autosave_ = 0; timeScale_ = 1;
  wanted_ = 0; wantedHeat_ = 0; sinceSeen_ = 1e9f; evadeT_ = 0; deathT_ = 0;
  policeSpawnT_ = 0; tutorialStep_ = 0; purchases_ = 0;
  fueling_ = {}; events_.clear(); tracers_.clear(); stains_.clear();
  subtitleText_.clear();subtitleSpeaker_.clear();subtitleUntil_=0;receiptUntil_=0;
  panel_ = {}; wheel_.open = false; wheel_.anim = 0; blur_ = 0;
  waypoint_ = {}; playerAnim_ = {}; npcAnim_.clear();
  audio_.loopStop(sirenHandle_); sirenHandle_ = 0;
  audio_.loopStop(surfHandle_); surfHandle_ = 0;
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
  player_.swimming=world_.waterDepth(p.x,p.y)>0.9f;
  player_.swimBlend=player_.swimming?1.0f:0.0f;
  player_.y = player_.swimming?world_.waterLevel-0.85f:world_.heightAt(p.x, p.y);
  player_.indoors = world_.inInterior(p.x, p.y);
}

// ------------------------------------------------------------------------------------------------ frame
void Game::frame(float dtReal, gfx::FrameData& fd) {
  dtReal = clamp(dtReal, 0.0005f, 0.1f);
  realTime_ += dtReal;
  lastDt_ = dtReal;
  fd_ = &fd;
  fd.clear();
  if (sessionActive_ && menu_ == MenuState::None && settings_.dayCycle) timeOfDay_ = std::fmod(timeOfDay_ + dtReal * dayRate_, 24.0f);
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
  updateStreaming();
  buildScene(fd);
  buildUi(fd, dtReal);
}

void Game::updatePlaying(float dtReal, const InputFrame& in) {
  // ---- fade transitions
  {
    float rate = settings_.reducedMotion ? 9.0f : 3.0f;
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
    wheel_.category = 0;
    wheel_.slots[0].clear();
    for (int w = 0; w < kWeaponCount; ++w)
      if (player_.owned[w]) wheel_.slots[0].push_back(w);   // weapon ids (category 0), item ids (category 1)
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
      int nSec = wheel_.category == 0 ? kWeaponCount : 8;
      int slot = (int)std::floor((ang + kTau / (2.0f * nSec)) / (kTau / nSec)) % nSec;
      wheel_.hovered = slot < (int)wheel_.slots[wheel_.category].size() ? slot : -1;
    } else if (len < R * 0.12f) {
      wheel_.hovered = -1;
    }
    (void)prevHover;
    if (in.wheelReleased || (!in.wheelHeld && !useScripted_)) {
      if (wheel_.hovered >= 0 && wheel_.hovered < (int)wheel_.slots[wheel_.category].size()) {
        int id = wheel_.slots[wheel_.category][wheel_.hovered];
        if (wheel_.category == 0) {
          if (id != player_.weapon) {
            equipWeapon(id);
            toast(std::string("Equipado: ") + weaponDef(id).name, weaponDef(id).icon);
          }
        } else useItem(id);
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
    if (in.interactPressed && focusValid_) { activateInteractable(focus_); interactPulse_ = 0.7f; }
  }

  // slow motion while the wheel is open
  float targetScale = wheel_.open ? 0.3f : 1.0f;   // the world keeps moving, slowly, while choosing
  timeScale_ += (targetScale - timeScale_) * expDecay(10.0f, dtReal);
  float dt = dtReal * timeScale_;
  time_ += dt;

  InputFrame gameIn = in;
  if (panel_.open || wheel_.open || fadeThen_ || fadeAlpha_ > 0.3f) {gameIn.move={};gameIn.runHeld=false;gameIn.attackHeld=false;gameIn.attackPressed=false;gameIn.reloadPressed=false;}

  updatePlayer(dt, gameIn);
  if (player_.swimming) {gameIn.attackPressed=false;gameIn.attackHeld=false;gameIn.reloadPressed=false;}
  updateCombat(dt, gameIn);
  updateVehicles(dt, gameIn);
  updateNpcs(dt);
  updateWanted(dt);
  updatePolice(dt);
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
      ci.focus = {player_.pos.x, player_.y + 0.55f*player_.swimBlend, player_.pos.y};
      ci.headingYaw = player_.yaw; ci.velocity = player_.vel; ci.speed = player_.speed;
    }
    ci.indoors = player_.indoors;
    ci.look = in.look; ci.zoomDelta = in.zoom; ci.userDragging = in.lookDragging;
    ci.shake = settings_.reducedMotion?0:cameraShake_;
    cam_.sensitivity = settings_.sensitivity;
    cam_.invertY = settings_.invertY;
    if (!panel_.open && !wheel_.open) cam_.update(dtReal, ci, world_, screenW_ / std::max(1.0f, screenH_));
    else { CameraInput frozen = ci; frozen.look = {}; frozen.zoomDelta = 0; cam_.update(dtReal, frozen, world_, screenW_ / std::max(1.0f, screenH_)); }
    cameraShake_ = std::max(0.0f, cameraShake_ - dtReal * 2.5f);
    audio_.setListener(cam_.focus(), cam_.right());
  }
  if ((world_.coastSide >= 0 && !player_.indoors && world_.beach.inflated(25).contains(player_.pos.x, player_.pos.y)) || player_.swimming) {
    Vec3 sound{player_.pos.x,world_.waterLevel,player_.pos.y};
    if(!surfHandle_){surfHandle_=audio_.loopStart("surf",sound,0.35f);if(settings_.soundCaptions)subtitle("Ondas do mar","Ambiente",true);}
    audio_.loopUpdate(surfHandle_,sound,0.35f);
  } else if(surfHandle_){audio_.loopStop(surfHandle_);surfHandle_=0;}
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
  if (adaptTimer_ < 3.0f || !settings_.dynamicRes) return;
  adaptTimer_ = 0;
  // dynamic resolution: never above the preset's base scale, never below 60% of it
  float base = preset().renderScale * settings_.resolution;
  float s = r_->renderScale();
  if (frameMsAvg_ > 36.0f && s > base * 0.6f + 0.01f) r_->setRenderScale(std::max(base * 0.6f, s - 0.08f));
  else if (frameMsAvg_ < 24.0f && s < base - 0.01f) r_->setRenderScale(std::min(base, s + 0.08f));
}

void Game::applySettings() {
  const QualityPreset& qp = preset();
  r_->setRenderScale(qp.renderScale * settings_.resolution);
  audio_.setMasterVolume(settings_.volume);
  r_->setShadowMapSize(qp.shadowMapSize);
  r_->setShadowsEnabled(settings_.shadows && qp.shadowCascades > 0);
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
  if (player_.entering || player_.exiting || fadeThen_ || player_.dead || player_.down) return;
  if (player_.vehicle >= 0) {
    Vehicle& v = vehicles_[player_.vehicle];
    if (std::fabs(v.speed) > 6.0f) { toast("Pare o veículo para sair", "car"); return; }
    if (fueling_.active) fueling_.active = false;
    player_.exiting = true;
    player_.transition = 0;
    player_.transitionVehicle = player_.vehicle;
    player_.transitionFrom=v.pos;
    const auto& def=vehicleDef(v.model);
    Vec2 cand[]={v.pos-right2(v.yaw)*(def.width*0.5f+0.85f),v.pos+right2(v.yaw)*(def.width*0.5f+0.85f),v.pos-fwd2(v.yaw)*(def.length*0.5f+0.9f)};
    player_.transitionTo=cand[0];for(Vec2 point:cand){auto free=point;phys::depenetrateCircle(world_,free,0.34f);player_.transitionTo=free;if((free-point).length()<0.05f)break;}
    player_.transitionFrom=lerp(v.pos,player_.transitionTo,0.75f);
    player_.vehicle=-1;v.occupant=-1;v.engineOn=false;player_.transitionYaw=player_.yaw;player_.vel={};
    return;
  }
  if (player_.indoors || player_.swimming) return;
  int vi = nearestVehicleTo(player_.pos, 2.4f);
  if (vi < 0) { toast("Nenhum veículo por perto", "car"); return; }
  if (vehicles_[vi].occupant >= 0) return;
  player_.entering = true;
  player_.transition = 0;
  player_.transitionVehicle = vi;
  player_.transitionFrom=player_.pos;player_.transitionYaw=player_.yaw;player_.vel={};
}

void Game::updatePlayer(float dt, const InputFrame& in) {
  Player& p = player_;
  p.hurtTimer = std::max(0.0f, p.hurtTimer - dt);
  // ---- enter / exit animation
  if (p.entering || p.exiting) {
    if(p.transitionVehicle<0||p.transitionVehicle>=(int)vehicles_.size()){p.entering=p.exiting=false;return;}
    p.transition = std::min(1.0f,p.transition + dt / 0.75f);
    Vehicle& v=vehicles_[p.transitionVehicle];const auto& d=vehicleDef(v.model);
    Vec2 door=v.pos-right2(v.yaw)*(d.width*0.5f+0.55f)+fwd2(v.yaw)*0.3f;
    float ease=smoothstep(p.transition);
    p.pos=lerp(p.transitionFrom,p.entering?door:p.transitionTo,ease);
    p.y=world_.heightAt(p.pos.x,p.pos.y);p.speed=0;p.running=false;
    p.yaw=lerpAngle(p.transitionYaw,p.entering?wrapAngle(v.yaw+kPi*0.5f):v.yaw,ease);
    if(p.transition>=1){
      if(p.entering){p.vehicle=p.transitionVehicle;v.occupant=0;v.engineOn=true;toast(std::string("Dirigindo: ")+d.name,"car");if(v.fuel<=0)toast("Sem combustível!","fuel",rgba(1.0f,0.5f,0.4f));if(settings_.soundCaptions)subtitle("Motor ligado","Veículo",true);}
      else {p.vehicle=-1;v.occupant=-1;v.engineOn=false;}
      p.entering=p.exiting=false;p.transition=0;p.targetYaw=p.yaw;
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

  // ---- on the floor / dead: no control (combat code slides and gets the character up)
  if (p.down || p.dead) {
    p.vel = {};
    p.speed = 0;
    p.running = false;
    p.y += (world_.heightAt(p.pos.x, p.pos.y) - p.y) * expDecay(18.0f, dt);
    return;
  }
  // ---- walking
  Vec2 mv = in.move;
  // attacking / hit / reloading slows the character; aiming turns it into a strafe
  float moveMul = 1.0f;
  if (p.attackT >= 0) moveMul = p.attackKind == 2 ? 0.05f : 0.3f;
  if (p.hitStun > 0) moveMul = std::min(moveMul, 0.15f);
  mv = mv * moveMul;
  float mag = std::min(1.0f, mv.length());
  Vec2 camF2 = {std::sin(cam_.yaw()), -std::cos(cam_.yaw())};
  Vec2 camR2 = {std::cos(cam_.yaw()), std::sin(cam_.yaw())};
  Vec2 dir = camF2 * mv.y + camR2 * mv.x;
  bool wantRun = in.runHeld && mag > 0.2f;
  bool canRun = (p.stamina > 6.0f || p.runBoost > 0.0f) && p.staminaCooldown <= 0.0f;
  p.swimming = world_.waterDepth(p.pos.x, p.pos.y) > (p.swimming ? 0.65f : 0.9f);
  p.running = wantRun && canRun;
  float maxSpeed = lerp(p.running ? 6.4f : 3.1f,p.running ? 2.8f : 1.8f,p.swimBlend);
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
        DamageInfo d;
        d.type = DamageType::RunOver;
        d.amount = closing * 3.0f;
        d.attacker = {ActorKind::Vehicle, v.id};
        d.dir = h.normal;
        d.knockback = std::min(8.0f, closing * 0.8f);
        applyDamage({ActorKind::Player, 0}, d);
        p.hurtTimer = 1.0f;
        if (!p.dead) toast("Ai! Cuidado com os carros", "heart", rgba(1.0f, 0.55f, 0.5f));
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
    p.pos.x = clamp(p.pos.x, world_.playArea.x0 + 0.7f, world_.playArea.x1 - 0.7f);
    p.pos.y = clamp(p.pos.y, world_.playArea.z0 + 0.7f, world_.playArea.z1 - 0.7f);
  }
  Vec2 actual = (p.pos - before) / std::max(dt, 1e-4f);
  p.speed = actual.length();
  if (p.aimHold > 0 && isFirearm(p.weapon)) p.targetYaw = yawFromDir(p.aimDir);   // keep facing the aim while strafing
  else if (p.speed > 0.35f && p.attackT < 0) {
    float want = yawFromDir(p.vel.length() > 0.2f ? p.vel : actual);
    p.targetYaw = want;
  }
  p.yaw = lerpAngle(p.yaw, p.targetYaw, expDecay(13.0f, dt));
  float stride = p.running ? 2.5f : 1.55f;
  if (p.speed > 0.25f) p.animTime += p.speed * dt / stride;
  p.swimming = world_.waterDepth(p.pos.x, p.pos.y) > (p.swimming ? 0.65f : 0.9f);
  p.swimBlend+=(float(p.swimming)-p.swimBlend)*expDecay(5.0f,dt);
  float hy = lerp(world_.heightAt(p.pos.x,p.pos.y),world_.waterLevel - 0.85f + 0.04f * std::sin(realTime_ * 2.2f),p.swimBlend);
  p.y += (hy - p.y) * expDecay(8.0f, dt);
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
      if (v.despawn || (v.police && v.driver >= 0 && v.occupant < 0)) continue;   // AI police cars are driven by updatePolice
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
          DamageInfo d;
          d.type = DamageType::Crash;
          d.amount = (impact - 7.0f) * 1.6f;
          d.attacker = {ActorKind::World, -1};
          applyDamage({ActorKind::Player, 0}, d);
          toast("Batida forte!", "car", rgba(1.0f, 0.55f, 0.45f));
        }
        if (impact > 4.0f) {
          emitEvent(EventKind::Crash, v.pos, 22.0f, {ActorKind::Player, 0}, {}, 0);
          audio_.play("crash", {v.pos.x, 0.8f, v.pos.y}, clamp(impact / 14.0f, 0.3f, 1.0f));
        }
      }
      // pedestrians
      if (std::fabs(v.speed) > 1.5f) {
        phys::OBB o = vehicleObb(v);
        for (Npc& n : npcs_) {
          if (n.interior) continue;
          if (n.state == NpcState::Dead) continue;
          phys::Hit hh = phys::circleVsObb(n.pos, 0.3f, o);
          if (!hh.hit) continue;
          n.pos += hh.normal * hh.depth;
          if (n.hitStun <= 0) {
            // run over: damage scales with the impact speed (central damage system)
            DamageInfo d;
            d.type = DamageType::RunOver;
            d.amount = std::fabs(v.speed) * 6.0f;
            d.attacker = driven ? ActorRef{ActorKind::Player, 0} : ActorRef{ActorKind::Vehicle, v.id};
            d.dir = hh.normal;
            d.knockback = std::min(9.0f, std::fabs(v.speed) * 0.7f);
            applyDamage({ActorKind::Npc, n.id}, d);
            n.hitStun = 1.0f;
            audio_.play("body", {n.pos.x, 0.9f, n.pos.y}, 0.9f);
            emitEvent(EventKind::RunOver, n.pos, 25.0f, d.attacker, {ActorKind::Npc, n.id}, 0);
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
    p.vel.y -= p.gravity * dt;
    p.pos += p.vel * dt;
    if (p.gravity > 0 && p.pos.y < 0.03f) { p.pos.y = 0.03f; p.vel = p.vel * 0.2f; }
    else p.size += dt * (p.gravity > 0 ? 0.05f : 0.7f);
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
      if (v.occupant >= 0 || player_.indoors || v.despawn || v.wrecked || (v.police && v.driver >= 0)) continue;
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
      if (n.interior != player_.indoors || n.despawn || n.state == NpcState::Dead || n.state == NpcState::Down || n.state == NpcState::Fight ||
          n.state == NpcState::Flee || n.police)
        continue;
      Interactable it;
      it.kind = IKind::Npc; it.id = (int)i; it.pos = {n.pos.x, 0, n.pos.y}; it.radius = n.role == 4 ? 4.6f : 2.3f;   // the clerk is reached across the counter
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
      it.kind = IKind::Door; it.id = d.id; it.pos = d.pos; it.radius = d.radius; it.label = d.toInterior ? "Entrar" : "Sair"; it.sub = d.toInterior && d.shop >= 0 ? world_.shops[d.shop].name : "Voltar à rua"; it.icon = "door";
      add(it);
    }
    if (player_.indoors)
      for (const ProductPoint& pr : world_.products) {
        const ItemDef& d = itemDef(pr.item);
        Interactable it;
        it.kind = IKind::Product; it.id = pr.id; it.pos = pr.pos; it.radius = 1.5f; it.label = "Comprar";
        int price=d.priceCents;
        if(pr.shop>=0) for(const auto& stock:world_.shops[pr.shop].stock) if(stock.kind==0&&stock.id==pr.item)price=stock.priceCents;
        it.sub = std::string(d.name) + " • " + fmtMoney(price); it.icon = "cart"; it.art = d.art ? d.art : "";
        it.enabled = moneyCents_ >= price;
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
      if (!d || fadeThen_ || player_.entering || player_.exiting) break;
      interactPulse_=0.8f;player_.vel={};player_.speed=0;
      DoorDef door = *d;
      fadeTarget_ = 1.0f;
      fadeThen_ = [this, door]() {
        teleportPlayer({door.arrive.x, door.arrive.z}, door.arriveYaw);
        CameraInput ci;
        ci.focus = {player_.pos.x, player_.y, player_.pos.y};
        ci.headingYaw = door.arriveYaw;
        ci.indoors = player_.indoors;
        cam_.snapTo(ci, world_, screenW_ / std::max(1.0f, screenH_));
        toast(door.toInterior && door.shop >= 0 ? world_.shops[door.shop].name : "Na rua", door.toInterior ? "cart" : "pin");
      };
      break;
    }
    case IKind::Product: buyItem(world_.products[it.id].item, true); break;
    case IKind::FuelPump: openFuelPanel(it.id); break;
    case IKind::Workshop: openWorkshopPanel(); break;
  }
}

}  // namespace gtabr
