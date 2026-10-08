// 3D model integration: Higgsfield characters (skinned, animated) and vehicles (body + procedural wheels, lights),
// LOD selection, reduced-rate NPC animation and dynamic lights (street lamps at night, headlights, brake lights).
#include <algorithm>
#include <cmath>

#include "../core/log.h"
#include "game.h"
#include "../core/fileio.h"

namespace gtabr {

namespace {
const char* kCharModels[] = {"protagonista", "frentista", "atendente", "pedestre_mulher", "mecanico", "pedestre_homem", "policial"};
const char* kCarModels[] = {"compacto", "sedan", "picape", "viatura"};
const char* kPropModels[] = {"mangueira", "coqueiro", "guardasol", "chafariz"};   // order = World::PropModel::kind
const char* kClipFiles[kClipCount] = {"data/models/anim_idle.ganim", "data/models/anim_walk.ganim", "data/models/anim_run.ganim",
                                     "data/models/anim_swim.ganim", "data/models/anim_swimidle.ganim", "data/models/anim_combat.ganim",
                                     "data/models/anim_die.ganim", "data/models/anim_turnl.ganim", "data/models/anim_turnr.ganim"};

uint32_t hashId(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}
float hash01(uint32_t x) { return (hashId(x) & 0xFFFFFF) / 16777215.0f; }

Vec3 paintColor(const char* name) {
  std::string n = name ? name : "";
  if (n == "branco") return {0.80f, 0.80f, 0.78f};
  if (n == "vermelho") return {0.42f, 0.03f, 0.02f};
  if (n == "prata") return {0.46f, 0.47f, 0.49f};
  if (n == "grafite") return {0.08f, 0.085f, 0.09f};
  if (n == "azul") return {0.03f, 0.10f, 0.34f};
  if (n == "vinho") return {0.22f, 0.02f, 0.04f};
  return {0.5f, 0.5f, 0.5f};
}

Mat4 rotX(float a) { return quatMatrix(Quat::axisAngle({1, 0, 0}, a)); }
Mat4 rotY(float a) { return quatMatrix(Quat::axisAngle({0, 1, 0}, a)); }
Mat4 rotZ(float a) { return quatMatrix(Quat::axisAngle({0, 0, 1}, a)); }
Mat4 scaleM(Vec3 s) { Mat4 m; m.at(0, 0) = s.x; m.at(1, 1) = s.y; m.at(2, 2) = s.z; return m; }
}  // namespace

void Game::queueModels() {
  // CPU side now (small files); textures go through the async texture queue.
  for (const char* n : kCharModels) {
    ModelAsset m;
    m.name = n;
    if (!loadModel(std::string("data/models/") + n + ".gmesh", m)) continue;
    charModels_.push_back(std::move(m));
  }
  for (const char* n : kCarModels) {
    ModelAsset m;
    m.name = n;
    loadModel(std::string("data/models/") + n + ".gmesh", m);
    carModels_.push_back(std::move(m));
  }
  for (const char* n : kPropModels) {
    ModelAsset m;
    m.name = n;
    if (fileio::assetExists(std::string("data/models/") + n + ".gmesh")) loadModel(std::string("data/models/") + n + ".gmesh", m);
    propModels_.push_back(std::move(m));
  }
  for (auto* list : {&charModels_, &carModels_, &propModels_})
    for (ModelAsset& m : *list) {
      if (!m.ok) continue;
      assets_.queueTexture("model:" + m.name + "_a", "models/" + m.name + "_a.gtex", gfx::SamplerKind::Repeat);
      assets_.queueTexture("model:" + m.name + "_n", "models/" + m.name + "_n.gtex", gfx::SamplerKind::Repeat);
      assets_.queueTexture("model:" + m.name + "_m", "models/" + m.name + "_m.gtex", gfx::SamplerKind::Repeat);
    }
  clips_.resize(kClipCount);
  bool clipsOk = true;
  for (int i = 0; i < kClipCount; ++i) {
    bool ok = loadClip(kClipFiles[i], clips_[i]);
    if (i < kClipRequired) clipsOk &= ok;
    else if (!ok) LOGW("optional clip %s missing (the graph falls back)", kClipFiles[i]);
  }
  if (!clipsOk) { clips_.clear(); LOGE("animation clips missing - characters fall back to sprites"); }
  else {
    clips_.resize(kClipCount + kActCount);
    for (int a = 0; a < kActCount; ++a)
      if (!loadClip(std::string("data/models/anim_") + actionFile(a) + ".ganim", clips_[kClipCount + a]))
        LOGW("action clip %s missing (gameplay still runs, without that animation)", actionFile(a));
  }
}

void Game::finishModels() {
  for (auto* list : {&charModels_, &carModels_, &propModels_})
    for (ModelAsset& m : *list)
      if (m.ok)
        uploadModel(*r_, m, assets_.texture("model:" + m.name + "_a"), assets_.texture("model:" + m.name + "_n"),
                    assets_.texture("model:" + m.name + "_m"));
  if (!clips_.empty()) {
    for (ModelAsset& m : charModels_) bindClips(m, clips_);
    const ModelAsset* ref = charModel("protagonista");
    if (ref) {
      for (int i = kClipWalk; i <= kClipRun; ++i) clips_[i].groundSpeed = estimateGroundSpeed(ref->skel, ref->rootFix, clips_[i], ref->clipMap[i]);
      LOGI("clip ground speed: walk %.2f m/s, run %.2f m/s", clips_[kClipWalk].groundSpeed, clips_[kClipRun].groundSpeed);
    }
    animator_.init(&clips_);
  }
  // procedural wheel
  {
    std::vector<gfx::ModelVertex> v;
    std::vector<uint32_t> idx;
    buildWheelMesh(v, idx);
    gfx::ModelLod lod{0, (uint32_t)idx.size()};
    wheelModel_ = r_->createModel(v.data(), v.size(), idx.data(), idx.size(), &lod, 1, false);
    std::vector<uint8_t> alb, orm;
    uint32_t w, h;
    buildWheelTextures(alb, orm, w, h);
    gfx::TexHandle ta = r_->createTextureRGBA(w, h, alb.data(), true, true, gfx::SamplerKind::Repeat);
    gfx::TexHandle tm = r_->createTextureRGBA(w, h, orm.data(), false, true, gfx::SamplerKind::Repeat);
    wheelMaterial_ = r_->createModelMaterial(ta, {}, tm);
  }
  modelsReady_ = !charModels_.empty() && !clips_.empty();
  carsReady_ = carModels_.size() >= 3 && carModels_[0].ok && carModels_[1].ok && carModels_[2].ok;
  LOGI("3D models: %zu characters, cars %s", charModels_.size(), carsReady_ ? "ok" : "missing");
}

const ModelAsset* Game::charModel(const std::string& name) const {
  for (const ModelAsset& m : charModels_)
    if (m.name == name && m.ok) return &m;
  return nullptr;
}

const ModelAsset* Game::modelForArchetype(const std::string& a, int id) const {
  const char* pick = nullptr;
  if (a == "player") pick = "protagonista";
  else if (a == "policial") pick = "policial";
  else if (a == "frentista") pick = "frentista";
  else if (a == "atendente") pick = "atendente";
  else if (a == "mecanico") pick = "mecanico";
  else if (a == "mulher_rosa" || a == "mulher_vestido") pick = "pedestre_mulher";
  else if (a == "vizinho" || a == "homem_polo" || a == "jovem_moletom" || a == "corredor") pick = "pedestre_homem";
  const ModelAsset* m = pick ? charModel(pick) : nullptr;
  if (m) return m;
  // fall back to the available bodies (never the protagonist for a pedestrian if anything else exists)
  static const char* menFallback[] = {"frentista", "atendente", "protagonista"};
  static const char* womenFallback[] = {"pedestre_mulher", "atendente"};
  bool woman = a.find("mulher") != std::string::npos;
  if (a == "mecanico") return charModel("frentista") ? charModel("frentista") : charModel("protagonista");
  if (a == "policial") return charModel("frentista") ? charModel("frentista") : charModel("protagonista");
  const char** list = woman ? womenFallback : menFallback;
  int n = woman ? 2 : 3;
  for (int k = 0; k < n; ++k)
    if (const ModelAsset* f = charModel(list[(k + id) % n])) {
      if (std::string(list[(k + id) % n]) == "protagonista" && n > 1 && k == 0) continue;
      return f;
    }
  return charModel("protagonista");
}

int Game::modelLod(const ModelAsset& m, float dist) const {
  float d = dist * preset().lodBias;
  float d0 = m.vehicle ? 22.0f : 12.0f, d1 = m.vehicle ? 55.0f : 30.0f;
  if (d < d0) return 0;
  if (d < d1 || m.lodCount < 3) return std::min(1, m.lodCount - 1);
  return std::min(2, m.lodCount - 1);
}

void Game::emitCharacter(gfx::FrameData& fd, const ModelAsset& m, CharAnim& a, Vec3 pos, float yaw, float scale, float speed, float dt,
                         bool fullRate, Vec4 tint, const AnimIn& in) {
  if (!m.ok || !m.gpu.valid()) return;
  float dist = (pos - cam_.eye()).length();
  // animation graph parameters for this frame
  a.airborne = in.airborne; a.airPhase = in.airPhase; a.dead = in.dead; a.combat = in.combat;
  if (dt > 1e-4f) {
    float raw = clamp(wrapAngle(yaw - a.lastYaw) / dt, -8.0f, 8.0f);
    a.turnRate += (raw - a.turnRate) * expDecay(12.0f, dt);
  }
  a.lastYaw = yaw;
  if (in.dead && in.req) *in.req = -1;   // a dead body is owned by the graph (death clip held on its last frame)
  // gameplay animation requests (attacks, hits, falls, reloads...)
  if (in.req && *in.req >= 0) {
    animator_.play(a, *in.req, in.reqSpeed, in.reqUpper, in.reqHold);
    *in.req = -1;
    fullRate = true;
  }
  // characters that came into view while lying down start in the final knocked-down pose
  if (in.lying && !in.dead && a.action != kActKnockDown) {
    animator_.play(a, kActKnockDown, 1.0f, false, true);
    a.actT = animator_.actionDuration(kActKnockDown);
    a.actW = 1.0f;
  }
  if (a.action >= 0) fullRate = fullRate || dist < 30.0f;
  // reduced update rate with distance: every frame near, 1/2 or 1/4 rate farther away
  float period = fullRate ? 0.0f : (dist < 25.0f ? 1.0f / 15.0f : 1.0f / 8.0f);
  a.accum += dt;
  bool eval = !a.valid || a.accum >= period;
  animator_.update(a, m, speed / std::max(0.5f, scale), dt, eval);
  if (eval) a.accum = 0;
  if (!a.valid) return;
  int off = (int)fd.bones.size();
  fd.bones.insert(fd.bones.end(), a.palette.begin(), a.palette.end());
  gfx::ModelDraw d;
  d.model = m.gpu;
  d.material = m.material;
  d.lod = modelLod(m, dist);
  d.transform = yawMatrix(pos, yaw) * scaleM({scale, scale, scale});
  if (in.rollPitch != 0.0f) d.transform = yawMatrix(pos, yaw) * Mat4::translation({0, 0.85f * scale, 0}) * rotX(-in.rollPitch) * Mat4::translation({0, -0.85f * scale, 0}) * scaleM({scale, scale, scale});   // tuck roll
  d.boneOffset = off;
  d.tint = tint;
  d.params = {0, 0, 1, 1};
  d.castShadow = dist < shadowRadius_ * 1.2f;
  fd.models.push_back(d);
  stats_.drawnModels++;
  // held weapon, attached to the right hand
  if (in.weapon > 0 && weaponMeshes_.ok && a.handValid && dist < 45.0f) {
    const bool gun = isFirearm(in.weapon);
    Mat4 w;
    auto frameFrom = [](Vec3 tip, Vec3 upHint, Vec3 origin) {
      // weapon space: barrel / blade along -Z, up +Y
      Vec3 z = tip * -1.0f;
      Vec3 x = upHint.cross(z);
      if (x.lengthSq() < 1e-5f) x = Vec3{1, 0, 0};
      x = x.normalized();
      Vec3 y = z.cross(x).normalized();
      Mat4 f;
      f.at(0, 0) = x.x; f.at(1, 0) = x.y; f.at(2, 0) = x.z;
      f.at(0, 1) = y.x; f.at(1, 1) = y.y; f.at(2, 1) = y.z;
      f.at(0, 2) = z.x; f.at(1, 2) = z.y; f.at(2, 2) = z.z;
      f.at(0, 3) = origin.x; f.at(1, 3) = origin.y; f.at(2, 3) = origin.z;
      return f;
    };
    if (gun && a.aim > 0.3f) {
      // aiming: the arm chain was rotated onto the aim direction, so the barrel simply follows that direction at the palm
      Vec3 fwd{0, std::sin(a.aimPitch), -std::cos(a.aimPitch)};
      w = frameFrom(fwd, {0, 1, 0}, a.handPos + a.handDir * 0.05f);
    } else {
      // held at the side: authored once against the rest-pose hand frame, then carried by the hand bone through every clip
      // (idle sway, walk swing, punch / slash, hit reactions). Melee points forward and slightly down; a lowered gun likewise.
      Vec3 restPalm = m.restHand.transformPoint({0, 0, 0}) + Vec3{0, -0.045f, 0};
      float out = restPalm.x < 0 ? -1.0f : 1.0f;
      Vec3 tip = gun ? Vec3{out * 0.10f, -0.35f, -0.93f} : Vec3{out * 0.28f, 0.22f, -0.93f};   // melee: raised a little and angled out of the body
      Mat4 rest = frameFrom(tip.normalized(), {0, 1, 0}, restPalm);
      w = a.handMat * inverseGeneral(m.restHand) * rest;
    }
    gfx::ModelDraw wd;
    wd.model = weaponMeshes_.mesh[in.weapon];
    wd.material = weaponMeshes_.material;
    wd.transform = d.transform * w;
    wd.params = {0, 0, 1, 1};
    wd.castShadow = dist < 20.0f;
    fd.models.push_back(wd);
  }
}

void Game::emitVehicle(gfx::FrameData& fd, int model, int color, Vec3 pos, float yaw, float pitch, float roll, float steer, float spin,
                       bool lightsOn, bool braking, int signal, bool reversing) {
  if (!carsReady_) return;
  // the police car uses its own model when available, otherwise a sedan in police white
  bool policeCar = model == kPoliceCarModel;
  int mi = policeCar && (carModels_.size() < 4 || !carModels_[3].ok) ? 1 : clamp(model, 0, (int)carModels_.size() - 1);
  const ModelAsset& m = carModels_[mi];
  float dist = (pos - cam_.eye()).length();
  const VehicleDef& vd = vehicleDef(model);
  Vec3 paint = paintColor(vd.colors[clamp(color, 0, 2)]);
  if (policeCar && mi == 1) paint = {0.85f, 0.85f, 0.84f};
  Mat4 body = yawMatrix(pos, yaw) * rotX(-pitch) * rotZ(roll);
  gfx::ModelDraw d;
  d.model = m.gpu;
  d.material = m.material;
  d.lod = modelLod(m, dist);
  d.transform = body;
  d.tint = {paint.x, paint.y, paint.z, 1.0f};
  d.params = {0, 1.0f, 1.0f, 1};   // clear coat on
  fd.models.push_back(d);
  stats_.drawnModels++;
  // wheels: spin with the travelled distance, front pair steers
  if (dist < 70.0f) {
    for (int i = 0; i < 4; ++i) {
      Vec3 c = m.wheel[i];
      if (c.lengthSq() < 1e-4f) continue;
      bool left = c.x < 0;
      bool front = i < 2;
      float r = std::max(0.15f, m.wheelRadius), w = std::max(0.12f, m.wheelWidth);
      Mat4 wm = body * Mat4::translation(c) * rotY(front ? -steer : 0.0f) * rotX(-spin) * scaleM({left ? -w : w, r, r});
      gfx::ModelDraw wd;
      wd.model = wheelModel_;
      wd.material = wheelMaterial_;
      wd.transform = wm;
      wd.tint = {0, 0, 0, 0};
      wd.params = {0, 0, 1, 1};
      wd.castShadow = dist < 40.0f;
      fd.models.push_back(wd);
    }
  }
  // lights: glow sprites (HDR) + dynamic lights collected for the light list
  auto glow = [&](Vec3 local, Vec3 rgb, float size, float intensity) {
    Vec3 wp = body.transformPoint(local);
    UvRect dot = assets_.icon("dot");
    if (!dot.valid) return;
    SpriteDef sd;
    sd.tex = assets_.iconsTex; sd.u0 = dot.u0; sd.v0 = dot.v0; sd.u1 = dot.u1; sd.v1 = dot.v1;
    sd.pivX = 0.5f; sd.pivY = 0.5f; sd.valid = true; sd.wm = sd.hm = size;
    uint32_t col = packRGBA8(rgb.x, rgb.y, rgb.z, 1.0f);
    addSprite(&sd, wp, 1.0f, 1.0f, false, col, false, false, intensity);
  };
  float night = day_.night;
  // police light bar: alternating red / blue strobes + coloured light on the surroundings
  if (policeCar && signal == 99) {
    float t = std::fmod(realTime_ * 2.6f, 1.0f);
    bool redOn = t < 0.5f;
    float roof = m.bounds.mx.y + 0.05f;
    Vec3 lr{-0.32f, roof, 0.1f}, rr{0.32f, roof, 0.1f};
    glow(lr, {1.0f, 0.08f, 0.05f}, redOn ? 0.7f : 0.25f, redOn ? 18.0f : 2.0f);
    glow(rr, {0.1f, 0.25f, 1.0f}, redOn ? 0.25f : 0.7f, redOn ? 2.0f : 18.0f);
    Vec3 c = body.transformPoint({0, roof, 0});
    pendingLights_.push_back({c, {0, 0, 0}, redOn ? Vec3{7.0f, 0.4f, 0.3f} : Vec3{0.4f, 1.0f, 8.0f}, 12.0f, -2.0f, dist * 0.5f});
    signal = 0;
  }
  for (int s = 0; s < 2; ++s) {
    Vec3 hl = m.headlight[s], tl = m.taillight[s];
    if (lightsOn && night > 0.2f && hl.lengthSq() > 1e-4f) {
      glow(hl + Vec3{0, 0, -0.05f}, {1.0f, 0.95f, 0.85f}, 0.55f, 10.0f * night);
      if (s == 0) {
        Vec3 mid = body.transformPoint((m.headlight[0] + m.headlight[1]) * 0.5f);
        Vec3 fwd = forwardFromYaw(yaw);
        pendingLights_.push_back({mid + fwd * 0.3f, fwd * 1.0f + Vec3{0, -0.18f, 0}, {9.0f, 8.4f, 7.2f}, 26.0f, 0.82f, dist});
      }
    }
    bool tailOn = (lightsOn && night > 0.2f) || braking;
    if (tailOn && tl.lengthSq() > 1e-4f) {
      float k = braking ? 6.0f : 2.0f;
      glow(tl + Vec3{0, 0, 0.05f}, {1.0f, 0.06f, 0.03f}, braking ? 0.45f : 0.32f, k);
      if (braking && s == 0) pendingLights_.push_back({body.transformPoint((tl + m.taillight[1]) * 0.5f) + Vec3{0, 0, 0}, {0, 0, 0}, {2.4f, 0.1f, 0.05f}, 5.0f, -2.0f, dist});
    }
    if (reversing && tl.lengthSq() > 1e-4f) glow(tl + Vec3{s ? -0.12f : 0.12f, 0, 0.06f}, {1, 1, 1}, 0.26f, 4.0f);
    // turn signals blink at 1.5 Hz on the side that is turning
    if (signal != 0 && std::fmod(realTime_ * 1.5f, 1.0f) < 0.5f) {
      bool leftSide = hl.x < 0;
      if ((signal < 0) == leftSide) {
        Vec3 amber{1.0f, 0.45f, 0.02f};
        if (hl.lengthSq() > 1e-4f) glow(hl + Vec3{leftSide ? -0.12f : 0.12f, 0, 0}, amber, 0.3f, 5.0f);
        if (tl.lengthSq() > 1e-4f) glow(tl + Vec3{leftSide ? -0.1f : 0.1f, 0, 0}, amber, 0.28f, 5.0f);
      }
    }
  }
}

void Game::emitModels(gfx::FrameData& fd, float dt) {
  stats_.drawnModels = 0;
  pendingLights_.clear();
  const bool indoors = player_.indoors;
  const Frustum& fr = cam_.frustum();
  const float drawDist = preset().drawDistance * settings_.drawDistance;
  const Vec3 eye = cam_.eye();
  auto visible = [&](Vec3 p, float r) {
    if ((p - eye).lengthSq() > drawDist * drawDist) return false;
    return fr.intersectsSphere({p.x, p.y + r * 0.5f, p.z}, r);
  };

  if (modelsReady_) {
    // ---- player
    if (player_.vehicle < 0) {
      const ModelAsset* m = modelForArchetype("player", 0);
      Vec3 pos{player_.pos.x, player_.y + player_.air, player_.pos.y};
      CharAnim& a = playerAnim_;
      a.swimming = player_.swimming;
      a.refuelTarget = fueling_.active ? 1.0f : 0.0f;
      a.talkTarget = (panel_.open && !panel_.portrait.empty() && !fueling_.active) ? 0.6f : 0.0f;
      a.reachTarget = interactPulse_ > 0 ? 1.0f : 0.0f;
      a.crouchTarget = (player_.entering || player_.exiting) ? 1.0f : 0.0f;
      float turn = wrapAngle(player_.targetYaw - player_.yaw);
      a.leanTarget = clamp(turn * 0.25f, -0.12f, 0.12f) * clamp(player_.speed / 3.0f, 0.0f, 1.0f);
      AnimIn ai;
      ai.req = &player_.animReq; ai.reqSpeed = player_.animReqSpeed; ai.reqUpper = player_.animReqUpper; ai.reqHold = player_.animReqHold;
      ai.lying = player_.down && player_.animReq < 0 && a.action != kActKnockDown && player_.dead;
      ai.weapon = player_.weapon;
      a.guardTarget = player_.blocking ? 1.0f : 0.0f;
      if (player_.dodgeT >= 0) {
        a.crouchTarget = 0.9f;
        float k = clamp(player_.dodgeT / 0.5f, 0.0f, 1.0f);
        ai.rollPitch = kTau * (k * k * (3.0f - 2.0f * k));
      }
      ai.airborne = player_.airborne; ai.airPhase = player_.airT / std::max(0.4f, player_.airDur); ai.dead = player_.dead;
      ai.combat = (player_.weapon > 0 && !isFirearm(player_.weapon)) || player_.attackT >= 0 || player_.comboWindow > 0;
      if (m && visible(pos, 2.0f)) emitCharacter(fd, *m, a, pos, player_.yaw, 1.0f, player_.speed, dt, true, {0, 0, 0, 0}, ai);
      interactPulse_ = std::max(0.0f, interactPulse_ - dt);
    }
    // ---- NPCs, nearest ones at full animation rate
    if (npcAnim_.size() != npcs_.size()) {
      size_t old = npcAnim_.size();
      npcAnim_.resize(npcs_.size());
      for (size_t i = old; i < npcs_.size(); ++i) {
        npcAnim_[i].rateScale = 0.9f + 0.2f * hash01((uint32_t)npcs_[i].id * 7 + 1);
        npcAnim_[i].t[kClipIdle] = hash01((uint32_t)npcs_[i].id * 13 + 5) * 3.0f;
        npcAnim_[i].t[kClipWalk] = hash01((uint32_t)npcs_[i].id * 17 + 3) * 2.0f;
      }
    }
    std::vector<std::pair<float, int>> order;
    for (size_t i = 0; i < npcs_.size(); ++i) {
      const Npc& n = npcs_[i];
      if (n.interior != indoors || n.despawn) continue;
      Vec3 pos{n.pos.x, n.y, n.pos.y};
      if (!visible(pos, 2.0f)) continue;
      order.push_back({(pos - eye).lengthSq(), (int)i});
    }
    std::sort(order.begin(), order.end());
    int full = preset().animFullRateNpcs;
    for (size_t k = 0; k < order.size(); ++k) {
      const Npc& n = npcs_[order[k].second];
      CharAnim& a = npcAnim_[order[k].second];
      const ModelAsset* m = modelForArchetype(n.archetype, n.id);
      if (!m) continue;
      Vec3 pos{n.pos.x, n.y, n.pos.y};
      float scale = (n.role == 0 ? 0.93f : 0.97f) + (n.role == 0 ? 0.14f : 0.06f) * hash01((uint32_t)n.id * 31 + 7);
      a.talkTarget = n.state == NpcState::Talk ? 1.0f : 0.0f;
      bool frentistaFuel = n.role == 1 && fueling_.active;
      a.refuelTarget = frentistaFuel ? 1.0f : 0.0f;
      a.reachTarget = (n.state == NpcState::Work && !frentistaFuel) ? 0.35f + 0.25f * std::sin(realTime_ * 0.7f + n.id) : 0.0f;
      a.headYawTarget = clamp(wrapAngle(n.lookYaw - n.yaw), -0.9f, 0.9f);
      a.waveTarget = (n.bubbleTimer > 0 && n.state != NpcState::Talk && n.speed < 0.2f) ? 1.0f : 0.0f;
      AnimIn ai;
      Npc& nm = npcs_[order[k].second];
      ai.req = &nm.animReq; ai.reqSpeed = nm.animReqSpeed; ai.reqUpper = nm.animReqUpper; ai.reqHold = nm.animReqHold;
      ai.lying = (n.state == NpcState::Down || n.state == NpcState::Dead) && n.animReq < 0;
      ai.weapon = n.weapon;
      ai.dead = n.state == NpcState::Dead;
      ai.combat = n.state == NpcState::Fight && !isFirearm(n.weapon);
      a.talkTarget = (n.state == NpcState::Talk || n.state == NpcState::CallPolice) ? 1.0f : 0.0f;
      if (n.state == NpcState::Chat && a.action != kActChat && animator_.hasAction(kActChat)) animator_.play(a, kActChat, a.rateScale, true);
      if (n.state != NpcState::Chat && a.action == kActChat) animator_.stop(a);
      a.aimTarget = (n.police && isFirearm(n.weapon) && n.state == NpcState::Fight) ? 1.0f : 0.0f;
      a.crouchTarget = n.state == NpcState::Cower ? 0.9f : 0.0f;
      // every pedestrian gets their own clothes (hue / saturation / brightness) and build; staff, police and the neighbour keep their look
      Vec4 look{0, 0, 0, 0};
      if (n.role == 0 && !n.police) {
        float hue = hash01((uint32_t)n.id * 977 + 13) * kTau;
        look = {hue, 0.75f + 0.55f * hash01((uint32_t)n.id * 131 + 5), 0.62f + 0.62f * hash01((uint32_t)n.id * 53 + 29), -1.0f};
      }
      emitCharacter(fd, *m, a, pos, n.yaw, scale, n.speed, dt, (int)k < full, look, ai);
      npcModelDrawn_[n.id] = true;
    }
  }

  // ---- Higgsfield trees and props (only where the chunk is drawn in full detail; far chunks keep the HLOD silhouettes)
  if (!indoors) {
    Vec3 focus = cam_.focus();
    for (const World::PropModel& p : world_.propModels) {
      if (p.kind < 0 || p.kind >= (int)propModels_.size()) continue;
      const ModelAsset& pm = propModels_[p.kind];
      if (!pm.ok || !pm.gpu.valid()) continue;
      float ccx = (std::floor(p.pos.x / World::kChunk) + 0.5f) * World::kChunk, ccz = (std::floor(p.pos.z / World::kChunk) + 0.5f) * World::kChunk;
      if (std::sqrt((ccx - focus.x) * (ccx - focus.x) + (ccz - focus.z) * (ccz - focus.z)) > lodDistance_) continue;
      float rad = pm.bounds.extent().length() * p.scale;
      if (!visible(p.pos + Vec3{0, rad * 0.5f, 0}, rad)) continue;
      float dist = (p.pos - cam_.eye()).length();
      gfx::ModelDraw d;
      d.model = pm.gpu;
      d.material = pm.material;
      d.lod = modelLod(pm, dist);
      d.transform = yawMatrix(p.pos, p.yaw) * scaleM({p.scale, p.scale, p.scale});
      d.params = {0, 0, 1, 1};
      d.castShadow = dist < shadowRadius_ * 1.3f;
      fd.models.push_back(d);
      stats_.drawnModels++;
    }
  }

  // ---- vehicles
  if (carsReady_ && !indoors) {
    for (const Vehicle& v : vehicles_) {
      if (v.despawn) continue;
      Vec3 pos{v.pos.x, world_.heightAt(v.pos.x, v.pos.y), v.pos.y};
      if (!visible(pos, 4.0f)) continue;
      int signal = 0;
      if (v.occupant >= 0 && std::fabs(v.speed) < 9.0f && std::fabs(v.steerInput) > 0.45f) signal = v.steerInput < 0 ? -1 : 1;
      if (v.police && v.siren) signal = 99;   // light bar
      emitVehicle(fd, v.model, v.color, pos, v.yaw, v.visualPitch, v.visualRoll, v.steerAngle, v.wheelSpin, v.engineOn, v.braking,
                  signal, v.speed < -0.3f);
    }
    for (const ParkedCarDef& p : world_.parked) {
      Vec3 pos{p.pos.x, world_.heightAt(p.pos.x, p.pos.z), p.pos.z};
      if (!visible(pos, 4.0f)) continue;
      emitVehicle(fd, p.model, p.color, pos, p.yaw, 0, 0, 0, 0, false, false, 0, false);
    }
  }

  // ---- street lamps at night
  if (day_.night > 0.05f && !indoors) {
    for (const Vec3& l : world_.lampLights) {
      float d = (l - cam_.focus()).length();
      if (d > 60.0f) continue;
      pendingLights_.push_back({l, {0, -1, 0}, Vec3{170.0f, 120.0f, 62.0f} * day_.night * (1.0f - 0.35f * rain_), 24.0f, 0.30f, d});
      UvRect dot = assets_.icon("dot");
      if (dot.valid) {
        SpriteDef sd;
        sd.tex = assets_.iconsTex; sd.u0 = dot.u0; sd.v0 = dot.v0; sd.u1 = dot.u1; sd.v1 = dot.v1;
        sd.pivX = 0.5f; sd.pivY = 0.5f; sd.valid = true; sd.wm = sd.hm = 0.9f;
        addSprite(&sd, l + Vec3{0, -0.1f, 0}, 1.0f, 1.0f, false, packRGBA8(1.0f, 0.82f, 0.55f, 1.0f), false, false, 8.0f * day_.night);
      }
    }
  }
  // ceiling lights of the shop interiors (only the room the player is in)
  if (indoors) {
    int room = world_.interiorAt(player_.pos.x, player_.pos.y);
    for (const Vec3& l : world_.interiorLights) {
      if (room < 0 || !world_.interiors[room].bounds.inflated(0.5f).contains(l.x, l.z)) continue;
      pendingLights_.push_back({l, {0, -1, 0}, {3.0f, 2.9f, 2.7f}, 7.5f, -2.0f, (l - Vec3{player_.pos.x, 1.5f, player_.pos.y}).length()});
    }
  }

  // keep the most relevant lights within the preset budget
  std::sort(pendingLights_.begin(), pendingLights_.end(), [](const PendingLight& a, const PendingLight& b) { return a.dist < b.dist; });
  int n = std::min<int>((int)pendingLights_.size(), std::min(preset().maxLights, gfx::kMaxLights));
  fd.lights.resize((size_t)n);
  for (int i = 0; i < n; ++i) {
    const PendingLight& L = pendingLights_[i];
    gfx::LightUBO& u = fd.lights[i];
    u.posRadius = {L.pos.x, L.pos.y, L.pos.z, L.radius};
    u.colorInt = {L.color.x, L.color.y, L.color.z, 0};
    Vec3 d = L.dir.lengthSq() > 1e-4f ? L.dir.normalized() : Vec3{0, -1, 0};
    u.dirCone = {d.x, d.y, d.z, L.cone};
  }
  if (getenv("GTABR_LIGHTLOG") && ((int)(realTime_ * 30) % 60) == 0) {
    LOGI("lights: pending %zu used %d maxLights %d night %.2f", pendingLights_.size(), n, preset().maxLights, day_.night);
    for (int i = 0; i < std::min(n, 3); ++i) LOGI("  L%d pos %.1f %.1f %.1f r %.1f col %.1f %.1f %.1f cone %.2f dist %.1f", i, pendingLights_[i].pos.x, pendingLights_[i].pos.y, pendingLights_[i].pos.z, pendingLights_[i].radius, pendingLights_[i].color.x, pendingLights_[i].color.y, pendingLights_[i].color.z, pendingLights_[i].cone, pendingLights_[i].dist);
  }
  fd.globals.lightInfo = {(float)n, settings_.reduceMotion ? wind_ * 0.35f : wind_, rain_, flash_};
}

}  // namespace gtabr

namespace gtabr {
// Dialogue portrait: the speaker's real 3D model, idling and talking, framed head and shoulders under a soft key light.
void Game::emitPortrait(gfx::FrameData& fd, float dt) {
  portraitLive_ = false;
  fd.portraitActive = false;
  if (!panel_.open || panel_.archetype.empty() || !r_) return;
  const ModelAsset* m = modelForArchetype(panel_.archetype, panel_.npcId >= 0 ? panel_.npcId : 0);
  if (!m || !m->ok || !m->gpu.valid()) return;
  if (portraitArch_ != panel_.archetype) { portraitAnim_ = CharAnim(); portraitArch_ = panel_.archetype; portraitT_ = 0; }
  portraitT_ += dt;
  Vec4 look{0, 0, 0, 0};
  if (panel_.npcId >= 0 && panel_.archetype.find("jovem") == std::string::npos) {
    for (const Npc& n : npcs_)
      if (n.id == panel_.npcId && n.role == 0 && !n.police) {
        float hue = hash01((uint32_t)n.id * 977 + 13) * kTau;
        look = {hue, 0.75f + 0.55f * hash01((uint32_t)n.id * 131 + 5), 0.62f + 0.62f * hash01((uint32_t)n.id * 53 + 29), -1.0f};
      }
  }
  size_t textLen = 0;
  for (unsigned char ch : panel_.text) if ((ch & 0xC0) != 0x80) ++textLen;
  bool speaking = panel_.reveal < (float)textLen;
  portraitAnim_.talkTarget = speaking ? 1.0f : 0.25f;
  portraitAnim_.headYawTarget = 0.18f * std::sin(portraitT_ * 0.7f);
  const size_t before = fd.models.size();
  AnimIn ai;
  const float yaw = 3.14159265f + 0.30f;   // faces the camera, turned a little for a three-quarter view
  emitCharacter(fd, *m, portraitAnim_, {0, 0, 0}, yaw, 1.0f, 0.0f, dt, true, look, ai);
  if (fd.models.size() == before) return;
  for (size_t i = before; i < fd.models.size(); ++i) { gfx::ModelDraw d = fd.models[i]; d.lod = 0; d.castShadow = false; fd.portraitModels.push_back(d); }
  fd.models.resize(before);
  // camera and studio lighting
  gfx::GlobalsUBO g = fd.globals;
  const Vec3 target{0.0f, 1.50f, 0.0f};
  const Vec3 eye{0.0f, 1.55f, 1.25f};
  Mat4 view = Mat4::lookAt(eye, target, {0, 1, 0});
  Mat4 proj = Mat4::perspective(0.50f, 1.0f, 0.1f, 30.0f);
  g.view = view; g.viewProj = proj * view;
  g.camPos = {eye.x, eye.y, eye.z, realTime_};
  g.camRight = {1, 0, 0, 0}; g.camUp = {0, 1, 0, 0}; g.camFwd = {0, 0, -1, 0};
  Vec3 L = Vec3{-0.55f, 0.45f, 0.70f}.normalized();
  g.sunDir = {L.x, L.y, L.z, 0.0f};
  g.sunColor = {2.5f, 2.25f, 2.0f, 0.0f};
  g.ambSky = {0.95f, 1.05f, 1.2f, 0}; g.ambGround = {0.35f, 0.31f, 0.28f, 0};
  g.sky0 = {0.30f, 0.42f, 0.70f, 0}; g.sky1 = {0.75f, 0.80f, 0.88f, 0};
  g.fog.w = 0.0f;
  g.params.z = 0.0f; g.params.w = 0.0f;
  g.cascade.w = 0.0f;
  g.probeInfo.x = 0.0f;
  g.lightInfo.x = 0.0f;
  fd.portraitGlobals = g;
  fd.portraitBg = {0.12f, 0.14f, 0.18f, 1.0f};
  fd.portraitActive = true;
  portraitLive_ = true;
}
}  // namespace gtabr
