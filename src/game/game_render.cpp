// Scene submission: world chunks (culled), billboard sprites with direction/LOD selection, ground shadows.
#include <algorithm>
#include <cmath>

#include "game.h"
#include "timeofday.h"

namespace gtabr {

namespace {
const char* kArchNames[10] = {"player", "frentista", "atendente", "mecanico", "vizinho",
                                      "mulher_rosa", "homem_polo", "jovem_moletom", "mulher_vestido", "corredor"};
}

int Game::archIndex(const std::string& a) const {
  for (int i = 0; i < kArch; ++i)
    if (a == kArchNames[i]) return i;
  return 6;
}

void Game::buildSpriteTables() {
  const char* anims[3] = {"idle", "walk", "run"};
  for (int a = 0; a < kArch; ++a) {
    CharSprites& cs = charSpr_[a];
    cs.dirs = a == 0 ? 16 : 8;
    for (int set = 0; set < 2; ++set)
      for (int an = 0; an < 3; ++an) {
        int frames = an == 0 ? 1 : 8;
        for (int f = 0; f < frames; ++f)
          for (int d = 0; d < cs.dirs; ++d) {
            std::string name = std::string(set ? "chr_high:" : "chr_low:") + "chr_" + kArchNames[a] + "_" + anims[an] + std::to_string(f) + "_" + std::to_string(d);
            cs.s[set][an][f][d] = assets_.sprite(name);
          }
      }
  }
  for (int m = 0; m < 3; ++m)
    for (int c = 0; c < 3; ++c) {
      for (int set = 0; set < 2; ++set) {
        int n = set ? 32 : 16;
        for (int d = 0; d < n; ++d) {
          std::string name = std::string(set ? "veh_high:" : "veh_low:") + "veh_" + vehicleDef(m).model + "_" + vehicleDef(m).colors[c] + "_" + std::to_string(d);
          vehSpr_[m][c].s[set][d] = assets_.sprite(name);
        }
      }
    }
  decorSpr_.clear();
  decorOf_.assign(world_.decor.size(), nullptr);
  for (size_t i = 0; i < world_.decor.size(); ++i) {
    const DecorInstance& d = world_.decor[i];
    auto it = decorSpr_.find(d.base);
    if (it == decorSpr_.end()) {
      DecorSprites ds;
      ds.dirs = d.dirCount;
      for (int set = 0; set < 2; ++set)
        for (int k = 0; k < d.dirCount; ++k)
          ds.s[set][k] = assets_.sprite(std::string(set ? "prp_high:" : "prp_low:") + d.base + "_" + std::to_string(k));
      it = decorSpr_.emplace(d.base, ds).first;
    }
  }
  for (size_t i = 0; i < world_.decor.size(); ++i) decorOf_[i] = &decorSpr_[world_.decor[i].base];
}

float Game::pitchHighWeight() const {
  float p = cam_.pitchDeg();
  return smoothstep((p - 30.0f) / (54.0f - 30.0f));
}

int Game::dirIndex(float objYaw, int n) const {
  float rel = wrapAngle(objYaw - cam_.yaw());
  int idx = (int)std::lround(rel / (kTau / n));
  return ((idx % n) + n) % n;
}

void Game::addSprite(const SpriteDef* d, Vec3 pos, float scale, float alpha, bool mirror, uint32_t rgb, bool silhouette, bool secondary,
                     float emissive) {
  if (!d || !d->valid || alpha < 0.01f) return;
  gfx::SpriteInst s{};
  s.pos[0] = pos.x; s.pos[1] = pos.y; s.pos[2] = pos.z;
  s.size[0] = d->wm * scale; s.size[1] = d->hm * scale;
  s.pivot[0] = mirror ? 1.0f - d->pivX : d->pivX; s.pivot[1] = d->pivY;
  s.uv[0] = mirror ? d->u1 : d->u0; s.uv[1] = d->v0; s.uv[2] = mirror ? d->u0 : d->u1; s.uv[3] = d->v1;
  uint32_t a = (uint32_t)clamp(alpha * 255.0f, 0.0f, 255.0f);
  s.tint = (rgb & 0x00FFFFFFu) | (a << 24);
  s.extra = emissive;
  int key = d->tex.id + (secondary ? 100000 : 0);
  (silhouette ? silBuckets_ : spriteBuckets_)[key].push_back(s);
}

void Game::addDecalEllipse(Vec3 pos, float hx, float hz, float alpha, float yaw, float kind) {
  gfx::DecalInst d{};
  d.pos[0] = pos.x; d.pos[1] = pos.y + 0.03f; d.pos[2] = pos.z;
  d.yaw = yaw; d.half[0] = hx; d.half[1] = hz; d.alpha = alpha; d.kind = kind;
  decals_.push_back(d);
}

void Game::setupGlobals(gfx::FrameData& fd) {
  gfx::GlobalsUBO& g = fd.globals;
  const float aspect = screenW_ / std::max(1.0f, screenH_);
  g.view = cam_.view();
  g.viewProj = cam_.viewProj();
  // inverse view-projection (for the sky): compute via analytic inverse of the pieces
  {
    // invert general 4x4 (small, called once per frame)
    const float* m = cam_.viewProj().m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    float id = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
    for (int i = 0; i < 16; ++i) g.invViewProj.m[i] = inv[i] * id;
  }
  g.camPos = {cam_.eye().x, cam_.eye().y, cam_.eye().z, realTime_};
  g.camRight = {cam_.right().x, cam_.right().y, cam_.right().z, 0};
  g.camUp = {cam_.up().x, cam_.up().y, cam_.up().z, 0};
  g.camFwd = {cam_.forward().x, cam_.forward().y, cam_.forward().z, 0};

  // ---- time of day
  day_ = computeDayLighting(timeOfDay_, cloudCover_);
  applyWeatherToLighting(day_);
  sunDir_ = day_.sunDir;
  const QualityPreset& qp = preset();
  float ind = indoorBlend_;

  // ---- cascaded sun shadows: a tight cascade around the focus, a wide one pushed ahead of the camera.
  // Both are orthographic and texel snapped so they do not shimmer while moving.
  Vec3 L = sunDir_.normalized();
  Vec3 lightRight = Vec3{0, 1, 0}.cross(L).normalized();
  Vec3 lightUp = L.cross(lightRight).normalized();
  Vec3 focus = cam_.focus();
  Vec3 fwdFlat = Vec3{cam_.forward().x, 0, cam_.forward().z};
  if (fwdFlat.lengthSq() < 1e-4f) fwdFlat = Vec3{std::sin(cam_.yaw()), 0, -std::cos(cam_.yaw())};
  fwdFlat = fwdFlat.normalized();
  const float smSize = (float)qp.shadowMapSize;
  auto cascadeMatrix = [&](Vec3 centre, float R) {
    float texel = 2.0f * R / smSize;
    float a = centre.dot(lightRight), b = centre.dot(lightUp);
    Vec3 snapped = centre + lightRight * (std::floor(a / texel) * texel - a) + lightUp * (std::floor(b / texel) * texel - b);
    Mat4 lview = Mat4::lookAt(snapped + L * 150.0f, snapped, {0, 1, 0});
    Mat4 lproj = Mat4::ortho(-R, R, -R, R, 20.0f, 320.0f);
    return lproj * lview;
  };
  const float r0 = lerp(26.0f, 16.0f, cam_.blend());
  const float r1 = 72.0f;
  g.lightViewProj[0] = cascadeMatrix(focus + fwdFlat * (r0 * 0.25f), r0);
  g.lightViewProj[1] = cascadeMatrix(focus + fwdFlat * (r1 * 0.55f), r1);
  shadowFocus_ = focus + fwdFlat * (r1 * 0.55f);
  shadowRadius_ = r1;

  const bool shadowsOn = settings_.shadows && qp.shadowCascades > 0;
  g.sunDir = {L.x, L.y, L.z, shadowsOn ? day_.shadowStrength * (1.0f - ind) : 0.0f};
  g.sunColor = {day_.sunColor.x, day_.sunColor.y, day_.sunColor.z, day_.sunDisk};
  g.ambSky = {day_.ambSky.x, day_.ambSky.y, day_.ambSky.z, 0};
  g.ambGround = {day_.ambGround.x, day_.ambGround.y, day_.ambGround.z, 0};
  g.fog = {day_.fog.x, day_.fog.y, day_.fog.z, lerp(day_.fogDensity, 0.0f, ind) * lerp(1.0f, 0.2f, cam_.isoAmount())};
  g.params = {day_.exposure, 1.25f / smSize, day_.night, ind};
  g.sky0 = {day_.zenith.x, day_.zenith.y, day_.zenith.z, day_.cloudCover};
  g.sky1 = {day_.horizon.x, day_.horizon.y, day_.horizon.z, day_.cloudBright};
  g.cascade = {0.0f, (float)std::max(1, qp.shadowCascades), 0.045f, wetness_};
  g.lightInfo = {0, 0, 0, 0};
  {
    const ProbeGrid& pg = world_.probes;
    bool on = probeTex_.valid() && pg.valid();
    g.probeRect = on ? Vec4{pg.x0 - pg.cell * 0.5f, pg.z0 - pg.cell * 0.5f, 1.0f / (pg.w * pg.cell), 1.0f / (pg.h * pg.cell)} : Vec4{0, 0, 1, 1};
    g.probeInfo = {on ? 1.0f : 0.0f, pg.groundY, pg.upperY, 0.0f};
  }
  fd.drawShadows = shadowsOn && ind < 0.5f && day_.shadowStrength > 0.01f;
  fd.shadowCascades = std::max(1, qp.shadowCascades);
  fd.vignette = 0.26f;
  fd.fade = fadeAlpha_;
  fd.blur = blur_;
  fd.dim = blur_ * 0.5f;
  // indoors: neutral lamp exposure
  fd.exposure = lerp(day_.exposure, 1.05f, ind) * settings_.brightness;
  fd.bloom = (qp.bloom && settings_.bloom) ? lerp(0.05f, 0.11f, day_.night) : 0.0f;
  fd.bloomThreshold = lerp(1.1f, 0.55f, day_.night);
  fd.lift = {day_.lift.x, day_.lift.y, day_.lift.z, lerp(day_.saturation, 1.0f, ind)};
  fd.gain = {day_.gain.x, day_.gain.y, day_.gain.z, day_.contrast};
  fd.worldMaterial = worldMaterial_;
  // SSAO + volumetric light shafts (need the scene depth; both skipped on the low presets)
  fd.nearZ = cam_.nearZ(); fd.farZ = cam_.farZ();
  fd.tanHalfY = std::tan(cam_.fov() * 0.5f);
  fd.tanHalfX = fd.tanHalfY * aspect;
  fd.aoStrength = qp.ao * (1.0f - 0.5f * ind);
  fd.aoRadius = 0.9f;
  fd.wetness = (settings_.reflections && qp.ao > 0.0f) ? wetness_ * (1.0f - ind) : 0.0f;
  {
    Vec3 U{0, 1, 0};
    fd.upView = {U.dot(cam_.right()), -U.dot(cam_.up()), U.dot(cam_.forward())};
  }
  {
    // the sun on screen: project a far point along the sun direction
    Vec3 sp = cam_.eye() + L * 2000.0f;
    const Mat4& vp = cam_.viewProj();
    float cx = vp.at(0, 0) * sp.x + vp.at(0, 1) * sp.y + vp.at(0, 2) * sp.z + vp.at(0, 3);
    float cy = vp.at(1, 0) * sp.x + vp.at(1, 1) * sp.y + vp.at(1, 2) * sp.z + vp.at(1, 3);
    float cw = vp.at(3, 0) * sp.x + vp.at(3, 1) * sp.y + vp.at(3, 2) * sp.z + vp.at(3, 3);
    float front = cw > 0.01f ? 1.0f : 0.0f;
    fd.sunUV = cw > 0.01f ? Vec2{cx / cw * 0.5f + 0.5f, cy / cw * 0.5f + 0.5f} : Vec2{0.5f, -2.0f};
    float up = clamp(L.y * 3.0f, 0.0f, 1.0f);                    // sun above the horizon
    float clear = (1.0f - day_.cloudCover * 0.8f) * (1.0f - clamp(rain_ * 1.5f, 0.0f, 1.0f));
    fd.shaftIntensity = qp.shafts * 0.55f * front * up * clear * (1.0f - day_.night) * (1.0f - ind);
    fd.shaftColor = day_.sunColor * 0.16f;
  }
}

void Game::emitWorld(gfx::FrameData& fd) {
  const Frustum& fr = cam_.frustum();
  bool indoors = player_.indoors;
  Vec3 focus = cam_.focus();
  stats_.drawnChunks = 0;
  for (const World::Chunk& c : world_.chunks) {
    if (c.interior != indoors) continue;
    const bool full = c.handle.valid();
    if (!full && !c.lodHandle.valid()) continue;
    Vec3 ctr = c.bounds.center();
    // HLOD: far (or not yet resident) chunks draw their merged low-detail mesh (one box per building, flat ground, tree silhouettes)
    float camD = std::sqrt((ctr.x - focus.x) * (ctr.x - focus.x) + (ctr.z - focus.z) * (ctr.z - focus.z));
    bool useLod = !full || (c.lodHandle.valid() && camD > lodDistance_);
    if (fr.intersects(c.bounds)) { fd.worldMeshes.push_back(useLod ? c.lodHandle.id : c.handle.id); stats_.drawnChunks++; }
    if (!full) continue;
    float dx = ctr.x - shadowFocus_.x, dz = ctr.z - shadowFocus_.z;
    float ext = (c.bounds.extent().x + c.bounds.extent().z) * 0.5f;
    if (std::sqrt(dx * dx + dz * dz) < shadowRadius_ * 1.42f + ext) fd.shadowMeshes.push_back(c.handle.id);
  }
  if (indoors && world_.interiorCeilingHandle.valid() && cam_.blend() > 0.45f && cam_.eye().y < 3.1f)
    fd.worldMeshes.push_back(world_.interiorCeilingHandle.id);
}

void Game::projectToScreen(const Vec3& p, Vec2& out, bool& visible) const {
  Vec4 c = cam_.viewProj() * Vec4(p, 1.0f);
  visible = c.w > 0.1f;
  if (!visible) return;
  out = {(c.x / c.w * 0.5f + 0.5f) * screenW_, (c.y / c.w * 0.5f + 0.5f) * screenH_};
}

void Game::emitSprites(gfx::FrameData& fd) {
  spriteBuckets_.clear();
  silBuckets_.clear();
  decals_.clear();
  const float wH = pitchHighWeight();
  const int prim = wH >= 0.5f ? 1 : 0;
  const float wp = prim ? wH : 1.0f - wH;
  const float secAlpha = 1.0f - wp;
  const Frustum& fr = cam_.frustum();
  const bool indoors = player_.indoors;
  const Vec3 eye = cam_.eye();
  const float drawDist = lerp(95.0f, 150.0f, cam_.blend());
  Vec3 L = sunDir_.normalized();
  Vec2 shadowDir = Vec2{-L.x, -L.z} / std::max(0.2f, L.y);   // metres of shadow per metre of height
  stats_.drawnSprites = 0;

  auto visible = [&](Vec3 p, float r) {
    if ((p - eye).lengthSq() > drawDist * drawDist) return false;
    return fr.intersectsSphere({p.x, p.y + r * 0.5f, p.z}, r);
  };
  auto emit = [&](const SpriteDef* lo, const SpriteDef* hi, Vec3 pos, float scale, float alpha, bool mirror, uint32_t rgb, bool sil) {
    const SpriteDef* pd = prim ? hi : lo;
    const SpriteDef* sd = prim ? lo : hi;
    addSprite(pd, pos, scale, alpha, mirror, rgb, sil, false);
    if (secAlpha > 0.02f) addSprite(sd, pos, scale, alpha * secAlpha, mirror, rgb, sil, true);
    stats_.drawnSprites++;
  };

  // ---- player
  if (player_.vehicle < 0) {
    const CharSprites& cs = charSpr_[0];
    int an = player_.speed < 0.3f ? 0 : (player_.running ? 2 : 1);
    int frame = an == 0 ? 0 : ((int)std::floor((player_.animTime - std::floor(player_.animTime)) * 8.0f)) & 7;
    int d = dirIndex(player_.yaw, cs.dirs);
    float alpha = 1.0f;
    if (player_.entering) alpha = 1.0f - smoothstep(player_.transition * 1.4f);
    if (player_.exiting) alpha = smoothstep(player_.transition * 3.0f);
    Vec3 pos{player_.pos.x, player_.y, player_.pos.y};
    if (!modelsReady_) {
      emit(cs.s[0][an][frame][d], cs.s[1][an][frame][d], pos, 1.0f, alpha, false, 0xFFFFFFFFu, false);
      addDecalEllipse({pos.x + shadowDir.x * 0.5f, pos.y, pos.z + shadowDir.y * 0.5f}, 0.5f, 0.38f, 0.5f * alpha, std::atan2(shadowDir.x, -shadowDir.y) , 0);
    } else {
      addDecalEllipse(pos, 0.32f, 0.32f, 0.35f, 0, 0);   // contact occlusion under the real shadow
    }
    if (!modelsReady_) emit(cs.s[0][an][frame][d], cs.s[1][an][frame][d], pos, 1.0f, alpha, false, 0xFFFFFFFFu, true);
  }

  // ---- NPCs
  for (const Npc& n : npcs_) {
    if (n.interior != indoors || n.despawn) continue;
    Vec3 pos{n.pos.x, n.y, n.pos.y};
    if (!visible(pos, 1.6f)) continue;
    if (modelsReady_) { addDecalEllipse(pos, 0.3f, 0.3f, 0.3f, 0, 0); continue; }
    int a = archIndex(n.archetype);
    const CharSprites& cs = charSpr_[a];
    int an = n.speed < 0.25f ? 0 : 1;
    int frame = an == 0 ? 0 : ((int)std::floor((n.animTime - std::floor(n.animTime)) * 8.0f)) & 7;
    int d = dirIndex(n.yaw, cs.dirs);
    emit(cs.s[0][an][frame][d], cs.s[1][an][frame][d], pos, 1.0f, 1.0f, false, 0xFFFFFFFFu, false);
    addDecalEllipse({pos.x + shadowDir.x * 0.5f, pos.y, pos.z + shadowDir.y * 0.5f}, 0.5f, 0.38f, 0.5f, std::atan2(shadowDir.x, -shadowDir.y), 0);
  }

  // ---- vehicles (drivable + parked scenery)
  auto vehicleSprite = [&](int model, int color, float yaw, Vec3 pos, float alpha, bool sil) {
    const VehSprites& vs = vehSpr_[std::min(model, 2)][color];
    int dl = dirIndex(yaw, 16), dh = dirIndex(yaw, 32);
    emit(vs.s[0][dl], vs.s[1][dh], pos, 1.0f, alpha, false, 0xFFFFFFFFu, sil);
  };
  auto vehicleShadow = [&](int model, Vec3 pos, float yaw) {
    const VehicleDef& vd = vehicleDef(model);
    float c = std::cos(yaw), s = std::sin(yaw);
    (void)c; (void)s;
    addDecalEllipse({pos.x + shadowDir.x * 0.55f, pos.y, pos.z + shadowDir.y * 0.55f}, vd.width * 0.5f + 0.12f, vd.length * 0.5f + 0.1f, 0.62f, yaw, 1);
  };
  if (!indoors) {
    for (const Vehicle& v : vehicles_) {
      if (v.despawn) continue;
      Vec3 pos{v.pos.x, world_.heightAt(v.pos.x, v.pos.y), v.pos.y};
      if (!visible(pos, 4.0f)) continue;
      if (!carsReady_) { vehicleSprite(v.model, v.color, v.yaw, pos, 1.0f, false); vehicleShadow(v.model, pos, v.yaw); }
      else addDecalEllipse(pos, vehicleDef(v.model).width * 0.5f, vehicleDef(v.model).length * 0.5f, 0.35f, v.yaw, 1);
      if (player_.vehicle == v.id) vehicleSprite(v.model, v.color, v.yaw, pos, 1.0f, true);
    }
    for (const ParkedCarDef& p : world_.parked) {
      Vec3 pos{p.pos.x, world_.heightAt(p.pos.x, p.pos.z), p.pos.z};
      if (!visible(pos, 4.0f)) continue;
      if (!carsReady_) { vehicleSprite(p.model, p.color, p.yaw, pos, 1.0f, false); vehicleShadow(p.model, pos, p.yaw); }
      else addDecalEllipse(pos, vehicleDef(p.model).width * 0.5f, vehicleDef(p.model).length * 0.5f, 0.35f, p.yaw, 1);
    }
    // ---- trees and props
    for (size_t i = 0; i < world_.decor.size(); ++i) {
      const DecorInstance& d = world_.decor[i];
      float reach = d.kind == DecorKind::Tree ? 4.5f : 1.8f;
      if (!visible(d.pos, reach)) continue;
      const DecorSprites* ds = decorOf_[i];
      if (!ds) continue;
      int dir = dirIndex(d.yaw, ds->dirs);
      emit(ds->s[0][dir], ds->s[1][dir], d.pos, d.scale, 1.0f, d.mirror && d.kind == DecorKind::Tree, 0xFFFFFFFFu, false);
      if (d.kind == DecorKind::Tree) {
        float h = d.base.find("palmeira") != std::string::npos ? 8.0f : (d.base.find("arbusto") != std::string::npos ? 0.9f : 6.4f) ;
        float r = d.base.find("arbusto") != std::string::npos ? 0.9f : 2.3f;
        addDecalEllipse({d.pos.x + shadowDir.x * h * 0.45f * d.scale, d.pos.y, d.pos.z + shadowDir.y * h * 0.45f * d.scale}, r * d.scale, r * d.scale * 0.85f, 0.38f, 0, 0);
      } else {
        addDecalEllipse({d.pos.x + shadowDir.x * 0.3f, d.pos.y, d.pos.z + shadowDir.y * 0.3f}, 0.5f, 0.5f, 0.3f, 0, 0);
      }
    }
  }
  // surface decals of the city (cracks, oil, manholes, drains, tyre marks, graffiti, grime, posters), faded with distance
  {
    Vec3 fc = cam_.focus();
    const float far2 = 62.0f * 62.0f;
    int cap = 260;
    for (const SurfaceDecal& sd : world_.decals) {
      float dx = sd.pos.x - fc.x, dz = sd.pos.z - fc.z;
      float d2 = dx * dx + dz * dz;
      if (d2 > far2) continue;
      if (indoors) break;
      float fade = 1.0f - smoothstep((std::sqrt(d2) - 40.0f) / 22.0f);
      if (!visible(sd.pos, std::max(sd.hx, sd.hz))) continue;
      gfx::DecalInst d{};
      d.pos[0] = sd.pos.x; d.pos[1] = sd.pos.y + (sd.vertical ? 0.0f : 0.012f); d.pos[2] = sd.pos.z;
      d.yaw = sd.yaw; d.half[0] = sd.hx; d.half[1] = sd.hz; d.alpha = sd.alpha * fade;
      d.kind = (float)(sd.kind + (sd.vertical ? 20 : 0));
      decals_.push_back(d);
      if (--cap <= 0) break;
    }
  }
  emitRain();
  // ---- smoke particles (use the soft dot of the icon atlas)
  UvRect dot = assets_.icon("dot");
  if (dot.valid) {
    SpriteDef sd;
    sd.tex = assets_.iconsTex; sd.u0 = dot.u0; sd.v0 = dot.v0; sd.u1 = dot.u1; sd.v1 = dot.v1; sd.pivX = 0.5f; sd.pivY = 0.5f; sd.valid = true;
    particles_.forEach([&](Particle& p, int) {
      float t = p.life / p.maxLife;
      sd.wm = sd.hm = p.size;
      uint32_t col = (p.color & 0x00FFFFFFu);
      addSprite(&sd, p.pos, 1.0f, t * 0.5f, false, col | 0xFF000000u, false, false);
    });
  }
}

void Game::flushSprites(gfx::FrameData& fd) {
  Vec3 f = cam_.forward(), e = cam_.eye();
  auto flush = [&](std::map<int, std::vector<gfx::SpriteInst>>& buckets, std::vector<gfx::SpriteInst>& dst, std::vector<gfx::Batch>& batches) {
    for (auto& [key, v] : buckets) {
      if (v.empty()) continue;
      std::sort(v.begin(), v.end(), [&](const gfx::SpriteInst& a, const gfx::SpriteInst& b) {
        float da = (a.pos[0] - e.x) * f.x + (a.pos[1] - e.y) * f.y + (a.pos[2] - e.z) * f.z;
        float db = (b.pos[0] - e.x) * f.x + (b.pos[1] - e.y) * f.y + (b.pos[2] - e.z) * f.z;
        return da > db;
      });
      gfx::Batch b;
      b.tex.id = key % 100000;
      b.first = (uint32_t)dst.size();
      b.count = (uint32_t)v.size();
      dst.insert(dst.end(), v.begin(), v.end());
      batches.push_back(b);
    }
  };
  flush(spriteBuckets_, fd.sprites, fd.spriteBatches);
  flush(silBuckets_, fd.silhouettes, fd.silhouetteBatches);
  fd.decals = decals_;
}

void Game::buildScene(gfx::FrameData& fd) {
  emitWorld(fd);
  emitSprites(fd);
  emitModels(fd, lastDt_);
  emitCombatVisuals(fd);
  flushSprites(fd);
}

}  // namespace gtabr
