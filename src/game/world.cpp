#include "world.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

#include "material_ids.h"
#include "props3d.h"

namespace gtabr {

namespace {

constexpr float SW = 2.5f;  // sidewalk width
constexpr float kH = World::kSidewalkH;

enum Side { N = 0, E = 1, S = 2, W = 3 };

struct Facade {
  int layer;
  Vec3 sideTint;
  float height;      // wall height
  bool flat;         // flat roof (laje) vs gable tile roof
  float aspectH;     // preferred height for this facade (stretch control)
};

// Lot-local frame for the special places: u runs along the street frontage, v goes from the street into the lot.
struct Frame {
  RectF L;
  bool frontS;   // the street is on the +z side of the lot
  float uOff = 0;
  Vec3 P(float u, float v, float y = 0) const { return {L.x0 + uOff + u, y, frontS ? L.z1 - v : L.z0 + v}; }
  RectF R(float u0, float v0, float u1, float v1) const {
    Vec3 a = P(u0, v0), b = P(u1, v1);
    return {std::min(a.x, b.x), std::min(a.z, b.z), std::max(a.x, b.x), std::max(a.z, b.z)};
  }
  int front() const { return frontS ? S : N; }
  float yawToStreet() const { return frontS ? 0.0f : kPi; }     // facing the street
  float yawFromStreet() const { return frontS ? kPi : 0.0f; }   // facing into the lot
};

const char* kCityNames[] = {"Porto Azul", "Vila Serena", "Santa Brisa", "Barra Clara", "São Vicente do Sul", "Praia Nova", "Monte Alegre",
                            "Ilhabela do Norte"};

class Gen {
 public:
  Gen(World& w, uint32_t seed) : w_(w), rng_(0xC0FFEE1234ull ^ ((uint64_t)seed * 0x9E3779B97F4A7C15ull)), seed_(seed) {}

  // ---- infrastructure --------------------------------------------------------------------------
  int chunkIndex(float x, float z) {
    int cx = (int)std::floor(x / World::kChunk), cz = (int)std::floor(z / World::kChunk);
    auto key = std::make_pair(cx, cz);
    auto it = map_.find(key);
    if (it != map_.end()) return it->second;
    w_.chunks.emplace_back();
    w_.chunks.back().cx = cx;
    w_.chunks.back().cz = cz;
    w_.chunks.back().interior = x >= World::kInteriorX - 60.0f;
    map_[key] = (int)w_.chunks.size() - 1;
    return (int)w_.chunks.size() - 1;
  }
  MeshBuilder mb(float x, float z, Vec3 tint = {1, 1, 1}) {
    MeshBuilder b(&w_.chunks[chunkIndex(x, z)].mesh);
    b.setTint(tint);
    return b;
  }
  MeshBuilder lodb(float x, float z, Vec3 tint = {1, 1, 1}) {
    MeshBuilder b(&w_.chunks[chunkIndex(x, z)].lod);
    b.setTint(tint);
    return b;
  }

  void ground(RectF r, float y, int layer, float tile, Vec3 tint = {1, 1, 1}, bool lod = true) {
    if (r.x1 - r.x0 < 1e-3f || r.z1 - r.z0 < 1e-3f) return;
    int cx0 = (int)std::floor(r.x0 / World::kChunk), cx1 = (int)std::floor((r.x1 - 1e-4f) / World::kChunk);
    int cz0 = (int)std::floor(r.z0 / World::kChunk), cz1 = (int)std::floor((r.z1 - 1e-4f) / World::kChunk);
    for (int cz = cz0; cz <= cz1; ++cz)
      for (int cx = cx0; cx <= cx1; ++cx) {
        float x0 = std::max(r.x0, cx * World::kChunk), x1 = std::min(r.x1, (cx + 1) * World::kChunk);
        float z0 = std::max(r.z0, cz * World::kChunk), z1 = std::min(r.z1, (cz + 1) * World::kChunk);
        if (x1 - x0 < 1e-4f || z1 - z0 < 1e-4f) continue;
        MeshBuilder b = mb((x0 + x1) * 0.5f, (z0 + z1) * 0.5f, tint);
        b.groundRect(x0, z0, x1, z1, y, layer, tile);
        if (lod) {
          MeshBuilder l = lodb((x0 + x1) * 0.5f, (z0 + z1) * 0.5f, tint);
          l.groundRect(x0, z0, x1, z1, y, layer, tile);
        }
      }
  }

  void collider(const AABB& b, ColKind k, int owner = -1) { w_.colliders.push_back({b, k, owner}); }

  void box(const Vec3& mn, const Vec3& mx, int layerSide, int layerTop, float tile, Vec3 tint, bool addCollider = true, ColKind kind = ColKind::Prop) {
    MeshBuilder b = mb((mn.x + mx.x) * 0.5f, (mn.z + mx.z) * 0.5f, tint);
    b.box(AABB(mn, mx), layerSide, layerTop, tile);
    if (addCollider) collider(AABB(mn, mx), kind);
  }

  void pole(float x, float z, float h, float r, int layer, Vec3 tint, bool col = true) {
    MeshBuilder b = mb(x, z, tint);
    b.prism({x, w_.heightAt(x, z), z}, r, h, 8, layer);
    if (col) collider(AABB({x - r, 0, z - r}, {x + r, h, z + r}), ColKind::Pole);
  }

  void decor(DecorKind k, const std::string& base, int dirs, Vec3 pos, float yaw, float scale = 1.0f, bool collide = false, float colR = 0.3f,
             float colH = 2.0f, int species = -1, int model = -1) {
    if (species >= 0) {
      MeshBuilder b = mb(pos.x, pos.z);
      MeshBuilder l = lodb(pos.x, pos.z);
      buildTree3D(b, &l, rng_, species, pos, yaw, scale);
    } else if (model >= 0) {
      MeshBuilder b = mb(pos.x, pos.z);
      buildProp3D(b, rng_, model, pos, yaw, scale);
    } else {
      DecorInstance d;   // sprite fallback (no 3D model for this one)
      d.kind = k; d.base = base; d.dirCount = dirs; d.pos = pos; d.yaw = yaw; d.scale = scale; d.mirror = rng_.chance(0.5f);
      w_.decor.push_back(d);
    }
    if (collide) collider(AABB({pos.x - colR, pos.y, pos.z - colR}, {pos.x + colR, pos.y + colH, pos.z + colR}), k == DecorKind::Tree ? ColKind::Tree : ColKind::Prop);
  }
  // type: 0 broadleaf, 1 palm, 2 shrub. 3D species: 0 mangueira, 1 ipe amarelo, 2 ipe roxo, 3 coqueiro, 4 palmeira imperial, 5 arbusto,
  // 6 amendoeira (beach almond), 7 flamboyant
  void tree(float x, float z, int type = 0, int forceSpecies = -1) {
    static const char* names[3] = {"arvore", "palmeira", "arbusto"};
    int variant = rng_.irange(0, 1);
    std::string base = std::string("tree_") + names[type] + std::to_string(variant);
    float s = type == 2 ? rng_.range(0.9f, 1.3f) : rng_.range(0.85f, 1.2f);
    int species = forceSpecies;
    if (species < 0) {
      if (type == 1) species = rng_.chance(0.6f) ? 3 : 4;
      else if (type == 2) species = 5;
      else {
        float r = rng_.uni();
        species = r < 0.45f ? 0 : (r < 0.6f ? 1 : (r < 0.72f ? 2 : (r < 0.86f ? 6 : 7)));
      }
    }
    decor(DecorKind::Tree, base, 4, {x, w_.heightAt(x, z), z}, rng_.range(0, kTau), s, type != 2, type == 2 ? 0.5f : 0.38f, 3.0f, species);
  }
  // 3D prop model ids: 0 lixeira, 1 banco, 2 poste (street lamp), 3 bomba, 4 orelhao, 5 hidrante, 6 cone, 7 guarda-sol, 8 cadeira de praia,
  // 9 quiosque, 10 vaso, 11 caixas, 12 pneus, 13 tambor, 14 placa de rua, 15 salva-vidas
  void prop(const char* name, float x, float z, float yaw, bool collide = true, float r = 0.35f, float h = 1.0f, int model = -1) {
    if (inFurniture_) for (const RectF& q : noTree_) if (q.contains(x, z)) return;
    static const std::pair<const char*, int> ids[] = {{"lixeira", 0}, {"banco", 1}, {"bomba", 3}, {"orelhao", 4}, {"hidrante", 5}, {"cone", 6},
                                                      {"vaso", 10}, {"caixas", 11}, {"pneus", 12}, {"tambor", 13}, {"semaforo", 16}, {"correio", 17}, {"onibus", 18}, {"outdoor", 19},
                                                      {"pare", 20}, {"carrinho", 21}, {"banca", 22}, {"bicicleta", 23}, {"canteiro", 26}};
    if (model < 0)
      for (auto& p : ids) if (std::string(p.first) == name) model = p.second;
    decor(DecorKind::Prop, std::string("prop_") + name, 8, {x, w_.heightAt(x, z), z}, yaw, 1.0f, collide, r, h, -1, model);
  }
  void parkedCar(int model, int color, float x, float z, float yaw) {
    ParkedCarDef p;
    p.model = model; p.color = color; p.pos = {x, 0, z}; p.yaw = yaw;
    w_.parked.push_back(p);
    float c = std::fabs(std::cos(yaw)), s = std::fabs(std::sin(yaw));
    static const float len[3] = {3.9f, 4.55f, 4.95f}, wid[3] = {1.66f, 1.78f, 1.82f};
    float hx = (len[model] * 0.5f) * s + (wid[model] * 0.5f) * c, hz = (len[model] * 0.5f) * c + (wid[model] * 0.5f) * s;
    collider(AABB({x - hx, 0, z - hz}, {x + hx, 1.5f, z + hz}), ColKind::Car, (int)w_.parked.size() - 1);
  }

  // ---- buildings ------------------------------------------------------------------------------------
  void wallsFor(const RectF& r, float h, int frontSide, int frontLayer, Vec3 tint, float vFrac = 1.0f) {
    MeshBuilder b = mb(r.cx(), r.cz(), {1, 1, 1});
    auto wallSeg = [&](int side, float x0, float z0, float x1, float z1) {
      float len = std::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
      if (side == frontSide) {
        b.setTint({1, 1, 1});
        b.wall(x0, z0, x1, z1, 0.0f, h, frontLayer, 0.003f, 0.997f, 1.0f - vFrac, 1.0f, 0.8f, 1.0f);
      } else {
        b.setTint(sideLayer_ == mat::brick_raw ? Vec3{1, 1, 1} : tint);
        { float tl = sideLayer_ == mat::brick_raw ? 1.1f : 4.0f; b.wall(x0, z0, x1, z1, 0.0f, h, sideLayer_, 0, len / tl, 0, h / tl, 0.78f, 1.0f); }
      }
    };
    wallSeg(S, r.x0, r.z1, r.x1, r.z1);
    wallSeg(E, r.x1, r.z1, r.x1, r.z0);
    wallSeg(N, r.x1, r.z0, r.x0, r.z0);
    wallSeg(W, r.x0, r.z0, r.x0, r.z1);
    // far LOD: one tinted box per building
    MeshBuilder l = lodb(r.cx(), r.cz(), tint * 0.9f);
    l.box(AABB({r.x0, 0, r.z0}, {r.x1, h, r.z1}), mat::wall_paint, mat::roof_laje, 4.0f);
  }

  void houseBuilding(const RectF& r, int front, const Facade& f, bool details = true) {
    float h = f.height;
    sideLayer_ = (f.layer == mat::house_periferia_a || f.layer == mat::house_periferia_b) ? mat::brick_raw : mat::wall_paint;
    wallsFor(r, h, front, f.layer, f.sideTint);
    sideLayer_ = mat::wall_paint;
    collider(AABB({r.x0, 0, r.z0}, {r.x1, h + 1.5f, r.z1}), ColKind::Building);
    w_.mapBuildings.push_back(r);
    MeshBuilder b = mb(r.cx(), r.cz(), {1, 1, 1});
    float cx = r.cx(), cz = r.cz();
    if (f.flat) {
      b.setTint({0.82f, 0.82f, 0.82f});
      b.roofRect(r.x0, r.z0, r.x1, r.z1, h, mat::roof_laje, 3.0f);
      b.setTint(f.sideTint);
      float p = 0.22f, ph = 0.7f;
      MeshBuilder pb = b;
      pb.box(AABB({r.x0, h, r.z0}, {r.x1, h + ph, r.z0 + p}), mat::wall_paint, mat::wall_paint, 4.0f);
      pb.box(AABB({r.x0, h, r.z1 - p}, {r.x1, h + ph, r.z1}), mat::wall_paint, mat::wall_paint, 4.0f);
      pb.box(AABB({r.x0, h, r.z0 + p}, {r.x0 + p, h + ph, r.z1 - p}), mat::wall_paint, mat::wall_paint, 4.0f);
      pb.box(AABB({r.x1 - p, h, r.z0 + p}, {r.x1, h + ph, r.z1 - p}), mat::wall_paint, mat::wall_paint, 4.0f);
      if (details && (r.x1 - r.x0) > 5.5f && rng_.chance(0.8f)) {
        // caixa d'agua (blue fibreglass water tank) on a small concrete base
        float tx = r.x0 + rng_.range(1.3f, (r.x1 - r.x0) - 1.3f), tz = r.z0 + rng_.range(1.3f, (r.z1 - r.z0) - 1.3f);
        MeshBuilder sb = mb(tx, tz, {0.7f, 0.7f, 0.7f});
        sb.box(AABB({tx - 0.9f, h, tz - 0.9f}, {tx + 0.9f, h + 0.35f, tz + 0.9f}), mat::concrete, mat::concrete, 2.0f);
        MeshBuilder tb = mb(tx, tz, {0.15f, 0.42f, 0.78f});
        tb.prism({tx, h + 0.35f, tz}, 0.85f, 1.1f, 12, mat::white);
        MeshBuilder lb = mb(tx, tz, {0.12f, 0.36f, 0.7f});
        lb.prism({tx, h + 1.45f, tz}, 0.9f, 0.12f, 12, mat::white);
      }
      if (details && rng_.chance(0.6f)) {
        // AC units on the side walls / roof
        float ax = r.x0 + rng_.range(0.8f, (r.x1 - r.x0) - 1.6f), az = r.z0 + rng_.range(0.8f, (r.z1 - r.z0) - 1.6f);
        MeshBuilder ab = mb(ax, az, {0.85f, 0.86f, 0.88f});
        ab.box(AABB({ax, h, az}, {ax + 0.9f, h + 0.55f, az + 0.45f}), mat::metal, mat::metal, 1.0f);
      }
      if (details && h > 9.0f && rng_.chance(0.5f)) {
        // antenna mast
        float ax = r.x0 + 1.0f, az = r.z0 + 1.0f;
        MeshBuilder ab = mb(ax, az, {0.35f, 0.35f, 0.38f});
        ab.prism({ax, h, az}, 0.04f, 2.6f, 6, mat::metal);
      }
    } else {
      bool ridgeAlongX = (front == N || front == S);
      float rise = (ridgeAlongX ? (r.z1 - r.z0) : (r.x1 - r.x0)) * 0.22f;
      b.setTint(rng_.chance(0.8f) ? Vec3{1, 1, 1} : Vec3{0.85f, 0.85f, 0.85f});
      int roofLayer = rng_.chance(0.78f) ? mat::roof_tile : mat::roof_fiber;
      float ov = 0.35f;
      b.gableRoof(r.x0, r.z0, r.x1, r.z1, h, rise, ridgeAlongX, roofLayer, 2.6f, ov);
      b.setTint(f.sideTint);
      auto tri = [&](Vec3 a, Vec3 c, Vec3 apex) {
        b.quad(a, c, apex, apex, {0, 1}, {1, 1}, {0.5f, 0}, {0.5f, 0}, mat::wall_paint, 0.8f, 0.8f, 1, 1);
      };
      if (ridgeAlongX) {
        tri({r.x1, h, r.z1}, {r.x1, h, r.z0}, {r.x1, h + rise, cz});
        tri({r.x0, h, r.z0}, {r.x0, h, r.z1}, {r.x0, h + rise, cz});
      } else {
        tri({r.x0, h, r.z1}, {r.x1, h, r.z1}, {cx, h + rise, r.z1});
        tri({r.x1, h, r.z0}, {r.x0, h, r.z0}, {cx, h + rise, r.z0});
      }
    }
  }

  // Front wall + gate of a house lot (muro com portao), on the sidewalk edge.
  void frontWall(const RectF& lot, int front, Vec3 tint) {
    const float h = rng_.range(1.4f, 2.1f), t = 0.18f;
    float gateW = 2.8f;
    auto seg = [&](float x0, float z0, float x1, float z1) {
      if (std::fabs(x1 - x0) < 0.2f && std::fabs(z1 - z0) < 0.2f) return;
      box({std::min(x0, x1), kH, std::min(z0, z1)}, {std::max(x0, x1) + (front == E || front == W ? t : 0), kH + h, std::max(z0, z1) + (front == N || front == S ? t : 0)},
          mat::wall_paint, mat::concrete, 3.0f, tint, true, ColKind::Wall);
    };
    if (front == N || front == S) {
      float z = front == N ? lot.z0 : lot.z1 - t;
      float g0 = lot.x0 + rng_.range(0.8f, std::max(0.9f, lot.w() - gateW - 0.8f));
      seg(lot.x0, z, g0, z);
      seg(g0 + gateW, z, lot.x1, z);
      // steel gate (darker, lower collision so it reads as a gate)
      MeshBuilder gb = mb(g0 + gateW * 0.5f, z, {0.22f, 0.24f, 0.27f});
      gb.box(AABB({g0, kH, z + 0.04f}, {g0 + gateW, kH + h * 0.95f, z + t - 0.04f}), mat::metal, mat::metal, 1.5f);
      collider(AABB({g0, 0, z}, {g0 + gateW, h, z + t}), ColKind::Wall);
    } else {
      float x = front == W ? lot.x0 : lot.x1 - t;
      float g0 = lot.z0 + rng_.range(0.8f, std::max(0.9f, lot.h() - gateW - 0.8f));
      seg(x, lot.z0, x, g0);
      seg(x, g0 + gateW, x, lot.z1);
      MeshBuilder gb = mb(x, g0 + gateW * 0.5f, {0.22f, 0.24f, 0.27f});
      gb.box(AABB({x + 0.04f, kH, g0}, {x + t - 0.04f, kH + h * 0.95f, g0 + gateW}), mat::metal, mat::metal, 1.5f);
      collider(AABB({x, 0, g0}, {x + t, h, g0 + gateW}), ColKind::Wall);
    }
  }

  Facade pickFacade(Rng& rng, int kind) {
    // kind 0 house, 1 sobrado, 2 apartment, 3 shop-house, 4 tall apartment, 5 warehouse
    switch (kind) {
      case 0: {
        if (periphery_ && rng.chance(0.75f)) {
          // self-built periphery houses: raw brick sides, flat slab roof with rebar, often a second floor
          static const Facade p[2] = {{mat::house_periferia_a, {0.72f, 0.42f, 0.3f}, 4.6f, true, 4.6f},
                                      {mat::house_periferia_b, {0.55f, 0.66f, 0.5f}, 6.4f, true, 6.4f}};
          return p[rng.irange(0, 1)];
        }
        static const Facade f[4] = {{mat::house_yellow, {0.93f, 0.78f, 0.28f}, 4.4f, false, 4.4f}, {mat::house_blue, {0.55f, 0.72f, 0.88f}, 4.4f, false, 4.4f},
                                    {mat::house_brick, {0.72f, 0.66f, 0.58f}, 4.4f, false, 4.4f}, {mat::house_modern, {0.92f, 0.92f, 0.9f}, 5.6f, true, 5.6f}};
        return f[rng.irange(0, 3)];
      }
      case 1: {
        static const Facade f[2] = {{mat::sobrado_pink, {0.9f, 0.55f, 0.5f}, 7.4f, true, 7.4f}, {mat::house_modern, {0.92f, 0.92f, 0.9f}, 6.6f, true, 6.6f}};
        return f[rng.irange(0, 1)];
      }
      case 2: {
        static const Facade f[3] = {{mat::apt_beige, {0.85f, 0.78f, 0.58f}, 12.0f, true, 12.0f}, {mat::apt_bands, {0.72f, 0.72f, 0.72f}, 11.0f, true, 11.0f},
                                    {mat::apt_green, {0.45f, 0.62f, 0.45f}, 10.0f, true, 10.0f}};
        return f[rng.irange(0, 2)];
      }
      case 4: {
        if (rng.chance(0.45f)) { Facade t{mat::apt_tower, {0.86f, 0.8f, 0.68f}, 26.0f * rng.range(0.85f, 1.3f), true, 26.0f}; return t; }
        static const Facade f[3] = {{mat::apt_beige, {0.85f, 0.78f, 0.58f}, 21.0f, true, 21.0f}, {mat::apt_bands, {0.72f, 0.72f, 0.72f}, 24.0f, true, 24.0f},
                                    {mat::apt_green, {0.45f, 0.62f, 0.45f}, 18.0f, true, 18.0f}};
        Facade x = f[rng.irange(0, 2)];
        x.height *= rng.range(0.85f, 1.25f);
        return x;
      }
      case 5: return {mat::wall_dark, {0.5f, 0.5f, 0.52f}, 7.0f, true, 7.0f};
      default: return {mat::apt_green, {0.45f, 0.62f, 0.45f}, 9.0f, true, 9.0f};
    }
  }

  // A row of plots along one block edge (facades face the street). tallness: 0 houses, 1 mixed, 2 centre, 3 seafront towers
  void plotRow(Side side, float a, float b, float fixed, float depth, int tallness, bool walls) {
    float pos = a;
    while (b - pos > 5.0f) {
      float wpl = tallness >= 2 ? rng_.range(10.0f, 15.0f) : rng_.range(7.0f, 10.5f);
      if (b - (pos + wpl) < 6.0f) wpl = b - pos;
      float d = depth + rng_.range(-1.0f, 1.0f);
      RectF lot;
      switch (side) {
        case N: lot = {pos, fixed, pos + wpl, fixed + d}; break;
        case S: lot = {pos, fixed - d, pos + wpl, fixed}; break;
        case W: lot = {fixed, pos, fixed + d, pos + wpl}; break;
        default: lot = {fixed - d, pos, fixed, pos + wpl}; break;
      }
      int front = side;
      float roll = rng_.uni();
      int kind;
      if (tallness == 0) kind = roll < 0.7f ? 0 : (roll < 0.9f ? 1 : 2);
      else if (tallness == 1) kind = roll < 0.45f ? 0 : (roll < 0.7f ? 1 : 2);
      else if (tallness == 2) kind = roll < 0.25f ? 1 : (roll < 0.7f ? 2 : 4);
      else kind = roll < 0.25f ? 2 : 4;
      Facade f = pickFacade(rng_, kind);
      f.height *= rng_.range(0.96f, 1.06f);
      // houses get a front yard behind a wall with a gate; apartments sit on the pavement line
      RectF r = lot;
      bool yard = (kind == 0 || kind == 1) && walls && d > 9.0f;
      if (yard) {
        float inset = rng_.range(2.2f, 3.2f);
        switch (side) {
          case N: r.z0 += inset; break;
          case S: r.z1 -= inset; break;
          case W: r.x0 += inset; break;
          default: r.x1 -= inset; break;
        }
        // shrink the house a little sideways to leave a garage passage
        if (side == N || side == S) r.x1 -= std::min(2.0f, r.w() * 0.2f); else r.z1 -= std::min(2.0f, r.h() * 0.2f);
        ground(RectF{lot.x0, lot.z0, lot.x1, lot.z1}, kH + 0.005f, rng_.chance(0.5f) ? mat::tile_floor : mat::concrete, 2.5f, {0.9f, 0.88f, 0.85f}, false);
        frontWall(lot, front, f.sideTint * 0.95f);
        if (rng_.chance(0.35f)) {
          // potted plant / small tree in the yard
          float px = (side == N || side == S) ? lot.x1 - 1.2f : lot.cx(), pz = (side == W || side == E) ? lot.z1 - 1.2f : lot.cz();
          tree(px, pz, 2);
        }
      }
      houseBuilding(r, front, f);
      if (!yard && rng_.chance(0.25f)) {
        float px = (side == N || side == S) ? r.x0 + 0.6f : (side == W ? r.x0 - 0.5f : r.x1 + 0.5f);
        float pz = (side == N) ? r.z0 - 0.5f : (side == S ? r.z1 + 0.5f : r.z0 + 0.6f);
        prop(rng_.chance(0.5f) ? "lixeira" : "vaso", px, pz, rng_.range(0, kTau), true, 0.32f, 1.0f);
      }
      pos += wpl;
    }
  }

  // ---- the plan: street grid, coast, districts ------------------------------------------------------
  std::vector<float> xs_, zs_, hwx_, hwz_;
  bool avX_[16] = {}, avZ_[16] = {};

  void plan() {
    // number and sizes of blocks vary with the seed; some lines are avenues (wider, with a median)
    int nx = rng_.irange(4, 6), nz = rng_.irange(4, 6);
    auto lines = [&](int n, std::vector<float>& c, std::vector<float>& hw, bool* av) {
      std::vector<float> sizes;
      for (int i = 0; i < n; ++i) sizes.push_back(rng_.range(40.0f, 62.0f));
      int avenue = rng_.irange(1, n - 1);
      float total = 0;
      for (float s : sizes) total += s;
      hw.assign(n + 1, 5.5f);
      for (int i = 0; i <= n; ++i) av[i] = false;
      hw[avenue] = 8.5f; av[avenue] = true;
      if (n >= 5 && rng_.chance(0.5f)) { int a2 = avenue >= 3 ? 1 : n - 1; hw[a2] = 8.0f; av[a2] = true; }
      for (float h : hw) total += h * 2;
      float x = -total * 0.5f;
      c.clear();
      for (int i = 0; i <= n; ++i) {
        x += hw[i];
        c.push_back(x);
        x += hw[i];
        if (i < n) x += sizes[i];
      }
    };
    lines(nx, xs_, hwx_, avX_);
    lines(nz, zs_, hwz_, avZ_);
    // coast: most cities touch the sea on one side
    w_.coastSide = rng_.chance(0.88f) ? rng_.irange(0, 3) : -1;
    if (w_.coastSide >= 0) {
      // the coast road becomes the "Avenida Beira-Mar"
      switch (w_.coastSide) {
        case 0: hwz_[0] = 8.0f; avZ_[0] = true; break;
        case 1: hwx_.back() = 8.0f; avX_[xs_.size() - 1] = true; break;
        case 2: hwz_.back() = 8.0f; avZ_[zs_.size() - 1] = true; break;
        default: hwx_[0] = 8.0f; avX_[0] = true; break;
      }
    }
    float x0 = xs_.front() - hwx_.front(), x1 = xs_.back() + hwx_.back();
    float z0 = zs_.front() - hwz_.front(), z1 = zs_.back() + hwz_.back();
    // an outer ring of land past the ring roads (backdrop lots) except on the coast
    const float ring = 24.0f;
    w_.land = {x0 - ring, z0 - ring, x1 + ring, z1 + ring};
    const float promenade = 7.0f, sand = rng_.range(28.0f, 40.0f), swim = 70.0f;
    beachDepth_ = promenade + sand;
    switch (w_.coastSide) {
      case 0: w_.land.z0 = z0; break;
      case 1: w_.land.x1 = x1; break;
      case 2: w_.land.z1 = z1; break;
      case 3: w_.land.x0 = x0; break;
      default: break;
    }
    w_.half = std::max(std::max(-w_.land.x0, w_.land.x1), std::max(-w_.land.z0, w_.land.z1)) + 2.0f;
    w_.playArea = w_.land.inflated(-1.0f);
    if (w_.coastSide >= 0) {
      // beach strip (promenade + sand) and the sea beyond it; shoreline is a signed coordinate along the coast axis
      RectF beach, sea;
      switch (w_.coastSide) {
        case 0: beach = {x0 - ring, z0 - beachDepth_, x1 + ring, z0}; sea = {x0 - ring - 60, z0 - beachDepth_ - 140, x1 + ring + 60, z0 - beachDepth_};
          w_.shoreline = -(z0 - beachDepth_); w_.playArea.z0 = z0 - beachDepth_ - swim; break;
        case 1: beach = {x1, z0 - ring, x1 + beachDepth_, z1 + ring}; sea = {x1 + beachDepth_, z0 - ring - 60, x1 + beachDepth_ + 140, z1 + ring + 60};
          w_.shoreline = x1 + beachDepth_; w_.playArea.x1 = x1 + beachDepth_ + swim; break;
        case 2: beach = {x0 - ring, z1, x1 + ring, z1 + beachDepth_}; sea = {x0 - ring - 60, z1 + beachDepth_, x1 + ring + 60, z1 + beachDepth_ + 140};
          w_.shoreline = z1 + beachDepth_; w_.playArea.z1 = z1 + beachDepth_ + swim; break;
        default: beach = {x0 - beachDepth_, z0 - ring, x0, z1 + ring}; sea = {x0 - beachDepth_ - 140, z0 - ring - 60, x0 - beachDepth_, z1 + ring + 60};
          w_.shoreline = -(x0 - beachDepth_); w_.playArea.x0 = x0 - beachDepth_ - swim; break;
      }
      w_.beach = beach;
      w_.sea = sea;
      w_.half = std::max(w_.half, std::max(std::max(-w_.playArea.x0, w_.playArea.x1), std::max(-w_.playArea.z0, w_.playArea.z1)) + 2.0f);
    }
    // streets
    for (size_t i = 0; i < xs_.size(); ++i) w_.roads.push_back({false, xs_[i], hwx_[i], z0, z1, avX_[i]});
    for (size_t j = 0; j < zs_.size(); ++j) w_.roads.push_back({true, zs_[j], hwz_[j], x0, x1, avZ_[j]});
    w_.cityName = kCityNames[seed_ % (sizeof(kCityNames) / sizeof(kCityNames[0]))];
  }

  bool nearIntersection(float v, float margin, bool alongX) {
    const auto& cs = alongX ? xs_ : zs_;
    const auto& hw = alongX ? hwx_ : hwz_;
    for (size_t i = 0; i < cs.size(); ++i)
      if (std::fabs(v - cs[i]) < hw[i] + margin) return true;
    return false;
  }

  void paint(RectF r, Vec3 col, float y = 0.012f) { ground(r, y, mat::white, 4.0f, col, false); }

  void zebra(float cx, float cz, bool alongX, float half) {
    const float stripe = 0.55f, gap = 0.55f, len = 3.0f;
    float start = -half + 0.6f;
    for (float o = start; o + stripe < half - 0.6f; o += stripe + gap) {
      if (alongX) paint({cx + o, cz - len * 0.5f, cx + o + stripe, cz + len * 0.5f}, {0.93f, 0.93f, 0.9f});
      else paint({cx - len * 0.5f, cz + o, cx + len * 0.5f, cz + o + stripe}, {0.93f, 0.93f, 0.9f});
    }
    RectF cross = alongX ? RectF{cx - half, cz - len * 0.5f, cx + half, cz + len * 0.5f} : RectF{cx - len * 0.5f, cz - half, cx + len * 0.5f, cz + half};
    w_.walkable.push_back(cross);
  }

  void streets() {
    float x0 = xs_.front() - hwx_.front(), x1 = xs_.back() + hwx_.back();
    float z0 = zs_.front() - hwz_.front(), z1 = zs_.back() + hwz_.back();
    for (size_t j = 0; j < zs_.size(); ++j) {
      RectF r{x0, zs_[j] - hwz_[j], x1, zs_[j] + hwz_[j]};
      ground(r, 0.0f, mat::asphalt, 6.0f);
      w_.lowRects.push_back(r);
      w_.mapRoads.push_back(r);
    }
    for (size_t i = 0; i < xs_.size(); ++i)
      for (size_t j = 0; j + 1 < zs_.size(); ++j) {
        RectF r{xs_[i] - hwx_[i], zs_[j] + hwz_[j], xs_[i] + hwx_[i], zs_[j + 1] - hwz_[j + 1]};
        ground(r, 0.0f, mat::asphalt, 6.0f);
        w_.lowRects.push_back(r);
        w_.mapRoads.push_back(r);
      }
    // cracked / patched asphalt
    for (int k = 0; k < 30; ++k) {
      const RoadLine& rl = w_.roads[rng_.irange(0, (int)w_.roads.size() - 1)];
      float t = rng_.range(rl.a + 10, rl.b - 10), o = rng_.range(-rl.hw + 2.5f, rl.hw - 2.5f);
      float px = rl.horizontal ? t : rl.c + o, pz = rl.horizontal ? rl.c + o : t;
      ground({px - 2.5f, pz - 2.5f, px + 2.5f, pz + 2.5f}, 0.004f, mat::asphalt_cracked, 5.0f, {0.95f, 0.95f, 0.95f}, false);
    }
    // markings: avenues get a yellow double line and a planted median; streets a dashed white line
    for (const RoadLine& rl : w_.roads) {
      for (float t = rl.a + 2; t < rl.b - 2; t += 6.0f) {
        if (nearIntersection(t, 7.5f, rl.horizontal) || nearIntersection(t + 3.0f, 7.5f, rl.horizontal)) continue;
        if (rl.avenue) {
          Vec3 col{1.0f, 0.82f, 0.1f};
          if (rl.horizontal) { paint({t, rl.c - 0.62f, t + 3.4f, rl.c - 0.5f}, col); paint({t, rl.c + 0.5f, t + 3.4f, rl.c + 0.62f}, col); }
          else { paint({rl.c - 0.62f, t, rl.c - 0.5f, t + 3.4f}, col); paint({rl.c + 0.5f, t, rl.c + 0.62f, t + 3.4f}, col); }
        } else {
          if (rl.horizontal) paint({t, rl.c - 0.08f, t + 3.0f, rl.c + 0.08f}, {0.95f, 0.95f, 0.92f});
          else paint({rl.c - 0.08f, t, rl.c + 0.08f, t + 3.0f}, {0.95f, 0.95f, 0.92f});
        }
      }
      if (rl.avenue) {
        // median strip with grass and palms between intersections
        const auto& cs = rl.horizontal ? xs_ : zs_;
        const auto& hw = rl.horizontal ? hwx_ : hwz_;
        for (size_t i = 0; i + 1 < cs.size(); ++i) {
          float a = cs[i] + hw[i] + 6.0f, b = cs[i + 1] - hw[i + 1] - 6.0f;
          if (b - a < 6) continue;
          RectF m = rl.horizontal ? RectF{a, rl.c - 0.45f, b, rl.c + 0.45f} : RectF{rl.c - 0.45f, a, rl.c + 0.45f, b};
          box({m.x0, 0, m.z0}, {m.x1, 0.18f, m.z1}, mat::concrete, mat::grass, 2.0f, {0.8f, 0.8f, 0.8f}, true, ColKind::Wall);
          for (float t = a + 4; t < b - 2; t += 12.0f) {
            float px = rl.horizontal ? t : rl.c, pz = rl.horizontal ? rl.c : t;
            tree(px, pz, 1, 4);
          }
        }
      }
    }
    // crosswalks + stop lines at every intersection arm
    for (size_t i = 0; i < xs_.size(); ++i)
      for (size_t j = 0; j < zs_.size(); ++j) {
        float cx = xs_[i], cz = zs_[j], hx = hwx_[i], hz = hwz_[j];
        if (j > 0) zebra(cx, cz - hz - 2.4f, true, hx);
        if (j + 1 < zs_.size()) zebra(cx, cz + hz + 2.4f, true, hx);
        if (i > 0) zebra(cx - hx - 2.4f, cz, false, hz);
        if (i + 1 < xs_.size()) zebra(cx + hx + 2.4f, cz, false, hz);
        paint({cx - hx, cz - hz - 4.6f, cx, cz - hz - 4.3f}, {0.95f, 0.95f, 0.92f});
        paint({cx, cz + hz + 4.3f, cx + hx, cz + hz + 4.6f}, {0.95f, 0.95f, 0.92f});
        paint({cx - hx - 4.6f, cz, cx - hx - 4.3f, cz + hz}, {0.95f, 0.95f, 0.92f});
        paint({cx + hx + 4.3f, cz - hz, cx + hx + 4.6f, cz}, {0.95f, 0.95f, 0.92f});
      }
  }

  // ---- blocks -------------------------------------------------------------------------------------------
  void sidewalkBands(const RectF& B, bool n, bool e, bool s, bool wst, int surface = mat::sidewalk) {
    float x0 = B.x0, x1 = B.x1, z0 = B.z0, z1 = B.z1;
    auto band = [&](RectF r, int curbSide) {
      ground(r, kH, surface, 3.0f);
      w_.walkable.push_back(r);
      w_.mapWalk.push_back(r);
      MeshBuilder b = mb(r.cx(), r.cz(), {0.78f, 0.78f, 0.78f});
      switch (curbSide) {
        case N: b.wall(r.x1, r.z0, r.x0, r.z0, 0, kH, mat::concrete, 0, (r.x1 - r.x0) / 3.0f, 0, 0.05f, 0.9f, 1.0f); break;
        case S: b.wall(r.x0, r.z1, r.x1, r.z1, 0, kH, mat::concrete, 0, (r.x1 - r.x0) / 3.0f, 0, 0.05f, 0.9f, 1.0f); break;
        case W: b.wall(r.x0, r.z0, r.x0, r.z1, 0, kH, mat::concrete, 0, (r.z1 - r.z0) / 3.0f, 0, 0.05f, 0.9f, 1.0f); break;
        default: b.wall(r.x1, r.z1, r.x1, r.z0, 0, kH, mat::concrete, 0, (r.z1 - r.z0) / 3.0f, 0, 0.05f, 0.9f, 1.0f); break;
      }
    };
    float zt = z0 + (n ? SW : 0), zb = z1 - (s ? SW : 0);
    if (n) band({x0, z0, x1, z0 + SW}, N);
    if (s) band({x0, z1 - SW, x1, z1}, S);
    if (wst) band({x0, zt, x0 + SW, zb}, W);
    if (e) band({x1 - SW, zt, x1, zb}, E);
  }

  RectF lotOf(const RectF& B) { return {B.x0 + SW, B.z0 + SW, B.x1 - SW, B.z1 - SW}; }

  void blockHouses(const RectF& B, int tallness, bool seafrontSouth = false) {
    sidewalkBands(B, true, true, true, true);
    RectF L = lotOf(B);
    int surface = rng_.chance(0.5f) ? mat::grass : mat::dirt;
    ground(L, kH, surface, 4.0f, surface == mat::grass ? Vec3{0.85f, 0.95f, 0.8f} : Vec3{0.9f, 0.85f, 0.8f});
    w_.mapGreen.push_back(L);
    float dN = tallness >= 2 ? 15.0f : 12.0f, dS = dN, dW = tallness >= 2 ? 14.0f : 11.0f, dE = dW;
    float zA = L.z0 + dN, zB = L.z1 - dS;
    (void)seafrontSouth;
    plotRow(N, L.x0, L.x1, L.z0, dN, tallness, true);
    plotRow(S, L.x0, L.x1, L.z1, dS, tallness, true);
    if (zB - zA > 6) {
      plotRow(W, zA, zB, L.x0, dW, std::min(tallness, 1), true);
      plotRow(E, zA, zB, L.x1, dE, std::min(tallness, 1), true);
    }
    RectF mid{L.x0 + dW + 1.5f, zA + 1.5f, L.x1 - dE - 1.5f, zB - 1.5f};
    if (mid.w() > 3 && mid.h() > 3) {
      int n = (int)(mid.w() * mid.h() / 60.0f);
      for (int k = 0; k < std::min(n, 6); ++k) tree(rng_.range(mid.x0, mid.x1), rng_.range(mid.z0, mid.z1), rng_.chance(0.3f) ? 1 : 0);
    }
  }

  void blockIndustrial(const RectF& B) {
    sidewalkBands(B, true, true, true, true);
    RectF L = lotOf(B);
    ground(L, kH, mat::concrete, 5.0f, {0.8f, 0.8f, 0.8f});
    w_.mapWalk.push_back(L);
    // two or three warehouses with loading yards
    int n = rng_.irange(2, 3);
    float w = L.w() / n;
    for (int i = 0; i < n; ++i) {
      RectF r{L.x0 + i * w + 1.5f, L.z0 + 6.0f, L.x0 + (i + 1) * w - 1.5f, L.z1 - 5.0f};
      Facade f = pickFacade(rng_, 5);
      f.height = rng_.range(6.5f, 9.0f);
      houseBuilding(r, N, f, false);
      prop("tambor", r.x0 + 1.0f, L.z0 + 3.0f, 0, true, 0.35f, 1.0f);
      prop("caixas", r.x1 - 1.5f, L.z0 + 3.0f, 0.4f, true, 0.6f, 1.0f);
    }
  }

  void blockParking(const RectF& B) {
    sidewalkBands(B, true, true, true, true);
    RectF L = lotOf(B);
    ground(L, 0.0f, mat::asphalt, 6.0f, {0.9f, 0.9f, 0.92f});
    w_.lowRects.push_back(L);
    w_.mapParking.push_back(L);
    for (float x = L.x0 + 2.0f; x < L.x1 - 2.0f; x += 2.7f)
      for (float zr : {L.z0 + 3.0f, L.cz() - 3.0f, L.cz() + 3.0f, L.z1 - 8.0f}) paint({x, zr, x + 0.12f, zr + 5.0f}, {0.95f, 0.95f, 0.92f});
    for (int k = 0; k < 9; ++k) {
      float x = L.x0 + 3.4f + rng_.irange(0, (int)((L.w() - 6) / 2.7f)) * 2.7f;
      float z = rng_.chance(0.5f) ? L.z0 + 5.5f : L.z1 - 5.5f;
      bool clash = false;
      for (const ParkedCarDef& p : w_.parked) if (std::fabs(p.pos.x - x) < 2.4f && std::fabs(p.pos.z - z) < 4.0f) clash = true;
      if (!clash) parkedCar(rng_.irange(0, 2), rng_.irange(0, 2), x, z, rng_.chance(0.5f) ? 0.0f : kPi);
    }
    for (float x : {L.x0 + 1.0f, L.x1 - 1.0f}) tree(x, L.cz(), 0);
  }

  void plaza(const RectF& B) {
    sidewalkBands(B, true, true, true, true);
    RectF L = lotOf(B);
    ground(L, kH, mat::grass, 4.0f, {0.88f, 0.98f, 0.82f});
    w_.mapGreen.push_back(L);
    RectF pl{L.x0 + 3.0f, L.z0 + 3.0f, L.x1 - 3.0f, L.z1 - 3.0f};
    RectF pathH{pl.x0, pl.cz() - 1.8f, pl.x1, pl.cz() + 1.8f};
    RectF pathV{pl.cx() - 1.8f, pl.z0, pl.cx() + 1.8f, pl.z1};
    for (const RectF& r : {pathH, pathV}) {
      ground(r, kH + 0.012f, mat::pedra_port, 3.0f);
      w_.walkable.push_back(r);
      w_.mapPlaza.push_back(r);
    }
    RectF centre{pl.cx() - 5.5f, pl.cz() - 5.5f, pl.cx() + 5.5f, pl.cz() + 5.5f};
    ground(centre, kH + 0.014f, mat::pedra_port, 3.0f, {1.0f, 1.0f, 1.0f});
    w_.walkable.push_back(centre);
    w_.mapPlaza.push_back(centre);
    w_.walkable.push_back(L);
    {
      // fountain: basin, water, central column
      MeshBuilder b = mb(pl.cx(), pl.cz(), {0.85f, 0.85f, 0.85f});
      b.box(AABB({pl.cx() - 1.8f, kH, pl.cz() - 1.8f}, {pl.cx() + 1.8f, kH + 0.6f, pl.cz() + 1.8f}), mat::concrete, mat::concrete, 2.0f);
      MeshBuilder b2 = mb(pl.cx(), pl.cz(), {0.3f, 0.55f, 0.65f});
      b2.groundRect(pl.cx() - 1.5f, pl.cz() - 1.5f, pl.cx() + 1.5f, pl.cz() + 1.5f, kH + 0.55f, mat::water, 3.0f);
      MeshBuilder b3 = mb(pl.cx(), pl.cz(), {0.8f, 0.8f, 0.78f});
      b3.prism({pl.cx(), kH + 0.5f, pl.cz()}, 0.32f, 1.8f, 12, mat::concrete);
      b3.prism({pl.cx(), kH + 2.3f, pl.cz()}, 0.7f, 0.18f, 12, mat::concrete);
      collider(AABB({pl.cx() - 1.8f, 0, pl.cz() - 1.8f}, {pl.cx() + 1.8f, 2.0f, pl.cz() + 1.8f}), ColKind::Prop);
    }
    for (int k = 0; k < 6; ++k) {
      float a = k * kTau / 6 + kPi / 6;
      prop("banco", pl.cx() + std::sin(a) * 7.5f, pl.cz() - std::cos(a) * 7.5f, -a, true, 0.8f, 0.9f);
    }
    prop("lixeira", pl.x0 + 1.0f, pl.cz() - 3.0f, 0, true, 0.4f, 1.0f);
    prop("lixeira", pl.x1 - 1.0f, pl.cz() + 3.0f, 0, true, 0.4f, 1.0f);
    prop("orelhao", pl.x1 - 0.8f, pl.z1 - 0.8f, kPi, true, 0.4f, 2.0f);
    prop("hidrante", pl.x0 + 0.8f, pl.z1 - 0.8f, 0, true, 0.2f, 0.8f);
    int nTrees = (int)(pl.w() * pl.h() / 70.0f);
    for (int k = 0; k < nTrees; ++k) {
      float x = rng_.range(pl.x0 + 1.5f, pl.x1 - 1.5f), z = rng_.range(pl.z0 + 1.5f, pl.z1 - 1.5f);
      if (pathH.inflated(1.4f).contains(x, z) || pathV.inflated(1.4f).contains(x, z) || centre.inflated(2.5f).contains(x, z)) continue;
      tree(x, z, k % 5 == 0 ? 1 : (k % 5 == 1 ? 2 : 0));
    }
    // kiosk (banca)
    RectF kiosk{pl.x0 + 1.0f, pl.z0 + 1.0f, pl.x0 + 5.0f, pl.z0 + 4.0f};
    Facade kf{mat::house_yellow, {0.95f, 0.7f, 0.3f}, 3.0f, false, 3.0f};
    houseBuilding(kiosk, S, kf, false);
    if (!placedNeighbour_) {
      w_.npcs.push_back({"vizinho", 3, {pl.cx() + 8.5f, kH, pl.cz() + 0.5f}, 1.6f});
      w_.neighbour = {pl.cx() + 8.5f, kH, pl.cz() + 0.5f};
      placedNeighbour_ = true;
    }
    w_.poiPlaza = {pl.cx(), kH, pl.cz() + 4.0f};
    w_.poiList.push_back(w_.poiPlaza);
    w_.pickupSpots.push_back({pl.x0 + 2.0f, kH, pl.z1 - 2.0f});
  }

  // Shop unit with its own facade texture on a centre street (enterable)
  void shopUnit(const RectF& lotRect, int front, int facadeLayer, ShopKind kind, const char* name) {
    Facade f{facadeLayer, {0.9f, 0.88f, 0.84f}, 5.0f, true, 5.0f};
    houseBuilding(lotRect, front, f);
    Vec3 door;
    switch (front) {
      case N: door = {lotRect.cx(), kH, lotRect.z0 - 0.9f}; break;
      case S: door = {lotRect.cx(), kH, lotRect.z1 + 0.9f}; break;
      case W: door = {lotRect.x0 - 0.9f, kH, lotRect.cz()}; break;
      default: door = {lotRect.x1 + 0.9f, kH, lotRect.cz()}; break;
    }
    addShop(kind, name, door, front);
  }

  // A commercial / centre block: the street fronts are ground-floor shops and taller buildings.
  void blockCentre(const RectF& B, int tallness) { blockHouses(B, tallness); }

  // ---- special places in lot-local frames ------------------------------------------------------------
  Frame frameFor(const RectF& L, bool frontS, float width) {
    Frame fr{L, frontS};
    fr.uOff = std::max(0.0f, (L.w() - width) * 0.5f);
    return fr;
  }

  void gasStation(const RectF& B, bool frontS) {
    sidewalkBands(B, true, true, true, true);
    RectF L = lotOf(B);
    Frame F = frameFor(L, frontS, 32.0f);
    ground(L, 0.0f, mat::asphalt, 6.0f, {0.92f, 0.92f, 0.92f});
    w_.lowRects.push_back(L);
    w_.mapParking.push_back(L);
    ground(F.R(2.5f, 1.0f, 25.5f, 20.0f), 0.01f, mat::garage_floor, 5.0f);
    w_.walkable.push_back(F.R(0.0f, 0.0f, 32.0f, 3.0f));
    // convenience store (enterable) at the back of the forecourt, facade toward the pumps
    RectF store = F.R(4.0f, 23.0f, 28.0f, std::min(31.0f, L.h() - 0.5f));
    Facade f{mat::shop_conveniencia, {0.92f, 0.92f, 0.9f}, 4.6f, true, 4.6f};
    houseBuilding(store, F.front(), f);
    w_.poiGasDoor = F.P(16.0f, 22.2f);
    addShop(ShopKind::Conveniencia, "Conveniência 24h", F.P(16.0f, 22.0f, 0.0f), F.front());
    // canopy strips over the pump islands
    float cy = 5.2f;
    for (float iv : {14.0f, 6.0f}) {
      RectF c = F.R(3.0f, iv - 2.4f, 25.0f, iv + 2.4f);
      MeshBuilder b = mb(c.cx(), c.cz(), {0.9f, 0.9f, 0.9f});
      b.box(AABB({c.x0, cy, c.z0}, {c.x1, cy + 0.45f, c.z1}), mat::white, mat::roof_metal, 4.0f);
      MeshBuilder s1 = mb(c.cx(), c.cz(), {0.07f, 0.22f, 0.62f});
      s1.box(AABB({c.x0 - 0.1f, cy + 0.02f, c.z0 - 0.1f}, {c.x1 + 0.1f, cy + 0.45f, c.z0}), mat::white, mat::white, 1.0f);
      s1.box(AABB({c.x0 - 0.1f, cy + 0.02f, c.z1}, {c.x1 + 0.1f, cy + 0.45f, c.z1 + 0.1f}), mat::white, mat::white, 1.0f);
      MeshBuilder s2 = mb(c.cx(), c.cz(), {0.98f, 0.78f, 0.1f});
      s2.box(AABB({c.x0 - 0.1f, cy - 0.14f, c.z0 - 0.1f}, {c.x1 + 0.1f, cy, c.z1 + 0.1f}), mat::white, mat::white, 1.0f);
    }
    for (float cu : {4.5f, 23.5f})
      for (float cv : {14.0f, 6.0f}) {
        Vec3 p = F.P(cu, cv);
        MeshBuilder c = mb(p.x, p.z, {0.88f, 0.88f, 0.9f});
        c.box(AABB({p.x - 0.3f, 0, p.z - 0.3f}, {p.x + 0.3f, cy, p.z + 0.3f}), mat::white, mat::white, 1.0f);
        collider(AABB({p.x - 0.3f, 0, p.z - 0.3f}, {p.x + 0.3f, cy, p.z + 0.3f}), ColKind::Pole);
      }
    int pid = (int)w_.pumps.size();
    for (float iv : {14.0f, 6.0f}) {
      RectF is = F.R(6.0f, iv - 0.9f, 22.0f, iv + 0.9f);
      MeshBuilder b = mb(is.cx(), is.cz(), {0.8f, 0.8f, 0.8f});
      b.box(AABB({is.x0, 0, is.z0}, {is.x1, 0.2f, is.z1}), mat::concrete, mat::sidewalk, 3.0f);
      collider(AABB({is.x0, 0, is.z0}, {is.x1, 0.3f, is.z1}), ColKind::Prop);
      for (float pu : {11.0f, 17.0f}) {
        Vec3 p = F.P(pu, iv);
        prop("bomba", p.x, p.z, 0, false, 0.35f, 1.0f, 3);
        collider(AABB({p.x - 0.4f, 0.2f, p.z - 0.3f}, {p.x + 0.4f, 1.8f, p.z + 0.3f}), ColKind::Prop);
        w_.pumps.push_back({pid++, {p.x, 0.2f, p.z}, 0});
      }
    }
    {
      Vec3 t = F.P(2.2f, 0.8f);
      MeshBuilder p = mb(t.x, t.z, {0.3f, 0.3f, 0.34f});
      p.prism({t.x, kH, t.z}, 0.18f, 8.0f, 8, mat::metal);
      MeshBuilder s = mb(t.x, t.z, {1, 1, 1});
      s.box(AABB({t.x - 1.6f, 7.4f, t.z - 0.3f}, {t.x + 1.6f, 9.4f, t.z + 0.3f}), mat::shop_posto, mat::white, 3.0f);
      collider(AABB({t.x - 0.2f, 0, t.z - 0.2f}, {t.x + 0.2f, 9, t.z + 0.2f}), ColKind::Pole);
    }
    Vec3 q;
    q = F.P(30.0f, 22.0f); prop("lixeira", q.x, q.z, 0, true, 0.4f, 1.0f);
    q = F.P(3.0f, 22.0f); prop("lixeira", q.x, q.z, 0, true, 0.4f, 1.0f);
    q = F.P(29.0f, 24.5f); prop("pneus", q.x, q.z, 0.5f, true, 0.6f, 1.0f);
    q = F.P(6.0f, 3.2f); prop("cone", q.x, q.z, 0, false);
    q = F.P(22.0f, 3.2f); prop("cone", q.x, q.z, 0, false);
    w_.poiGas = F.P(14.0f, 10.0f);
    w_.gasAttendant = F.P(14.0f, 10.2f, 0.2f);
    w_.gasLaneEntry = F.P(1.6f, 4.0f);
    w_.gasLanePump = F.P(11.5f, 10.0f);
    w_.gasLaneExit = F.P(27.2f, 10.0f);
    w_.gasApproach = F.P(1.6f, -5.0f);
    w_.gasLaneTurn = F.P(1.8f, 10.0f);
    w_.gasExitStreet = F.P(27.5f, -5.0f);
    noTree_.push_back(L.inflated(2.6f));
    Vec3 fp = F.P(13.5f, 9.6f, 0.2f);
    w_.npcs.push_back({"frentista", 1, fp, F.yawToStreet() + 1.2f});
    w_.mapBuildings.push_back(store);
    w_.poiList.push_back(F.P(14.0f, -1.5f, kH));
  }

  void market(const RectF& B, bool frontS) {
    sidewalkBands(B, true, true, true, true);
    RectF L = lotOf(B);
    Frame F = frameFor(L, frontS, 32.0f);
    ground(L, 0.0f, mat::asphalt, 6.0f, {0.9f, 0.9f, 0.92f});
    w_.lowRects.push_back(L);
    w_.mapParking.push_back(L);
    noTree_.push_back(L.inflated(2.6f));
    RectF bld = F.R(2.0f, 0.0f, 26.0f, 12.0f);
    Facade f{mat::shop_mercado, {0.95f, 0.85f, 0.55f}, 5.2f, true, 5.2f};
    houseBuilding(bld, F.front(), f);
    w_.poiMarket = F.P(14.0f, -2.5f);
    w_.poiMarketDoor = F.P(14.0f, -0.8f, kH);
    w_.marketParking = F.P(14.0f, -6.0f);   // street lane in front of the door
    marketSpot_ = F.P(9.0f, 17.0f);
    marketSpotYaw_ = F.yawToStreet();
    addShop(ShopKind::Mercado, "Mercado do Zé", F.P(14.0f, -1.0f, kH), F.front());
    float depth = L.h();
    for (float u = 2.0f; u < 30.5f; u += 2.7f) {
      if (depth > 30) paint(F.R(u, depth - 5.5f, u + 0.12f, depth - 0.5f), {0.95f, 0.95f, 0.92f});
      paint(F.R(u, 14.5f, u + 0.12f, 19.5f), {0.95f, 0.95f, 0.92f});
    }
    struct C { int m, c; float u, v; bool flip; };
    const C cars[] = {{1, 1, 3.4f, 22.0f, false}, {0, 1, 8.8f, 22.0f, false}, {2, 2, 17.0f, 22.0f, false}, {0, 2, 27.5f, 22.0f, false}, {1, 2, 20.0f, 17.0f, true}};
    for (const C& c : cars) {
      if (c.v + 3 > depth) continue;
      Vec3 p = F.P(c.u, c.v);
      parkedCar(c.m, c.c, p.x, p.z, F.yawToStreet() + (c.flip ? kPi : 0.0f));
    }
    Vec3 q;
    q = F.P(1.5f, 13.0f); prop("lixeira", q.x, q.z, 0, true, 0.4f, 1.0f);
    q = F.P(27.0f, 13.0f); prop("lixeira", q.x, q.z, 0, true, 0.4f, 1.0f);
    q = F.P(28.0f, 1.2f); prop("caixas", q.x, q.z, 0.3f, true, 0.6f, 1.0f);
    for (float u : {2.0f, 14.0f, 30.0f}) {
      Vec3 p = F.P(u, 13.5f);
      MeshBuilder b = mb(p.x, p.z, {0.5f, 0.5f, 0.5f});
      b.box(AABB({p.x - 1.0f, 0, p.z - 0.5f}, {p.x + 1.0f, 0.25f, p.z + 0.5f}), mat::concrete, mat::grass, 2.0f);
      tree(p.x, p.z, 0);
    }
    w_.poiList.push_back(F.P(14.0f, -1.5f, kH));
  }

  void workshop(const RectF& B, bool frontS) {
    sidewalkBands(B, true, true, true, true);
    RectF L = lotOf(B);
    Frame F = frameFor(L, frontS, 32.0f);
    float depth = std::min(31.0f, L.h());
    ground(F.R(-F.uOff, 0.0f, L.w() - F.uOff, 19.0f), 0.0f, mat::garage_floor, 5.0f);
    ground(F.R(-F.uOff, 19.0f, L.w() - F.uOff, L.h()), 0.0f, mat::asphalt, 6.0f);
    w_.lowRects.push_back(L);
    w_.mapParking.push_back(L);
    noTree_.push_back(L.inflated(2.6f));
    RectF bld = F.R(2.0f, 19.0f, 26.0f, depth);
    Facade f{mat::shop_oficina, {0.62f, 0.64f, 0.68f}, 5.5f, true, 5.5f};
    houseBuilding(bld, F.front(), f);
    RectF bay = F.R(6.0f, 11.5f, 22.0f, 18.5f);
    w_.serviceBay = bay;
    paint({bay.x0, bay.z0, bay.x1, bay.z0 + 0.18f}, {0.98f, 0.8f, 0.1f});
    paint({bay.x0, bay.z1 - 0.18f, bay.x1, bay.z1}, {0.98f, 0.8f, 0.1f});
    paint({bay.x0, bay.z0, bay.x0 + 0.18f, bay.z1}, {0.98f, 0.8f, 0.1f});
    paint({bay.x1 - 0.18f, bay.z0, bay.x1, bay.z1}, {0.98f, 0.8f, 0.1f});
    w_.poiWorkshop = F.P(14.0f, 15.0f);
    w_.workshopBayEntry = F.P(14.0f, 4.0f);
    w_.workshopApproach = F.P(14.0f, -5.0f);
    w_.workshopMechanic = F.P(24.2f, 16.5f);
    Vec3 q;
    q = F.P(27.0f, 21.0f); prop("pneus", q.x, q.z, 0.4f, true, 0.6f, 1.0f);
    q = F.P(27.8f, 24.0f); prop("tambor", q.x, q.z, 0, true, 0.35f, 1.0f);
    q = F.P(27.8f, 25.2f); prop("tambor", q.x, q.z, 0, true, 0.35f, 1.0f);
    q = F.P(5.0f, 10.0f); prop("cone", q.x, q.z, 0, false);
    q = F.P(23.0f, 10.0f); prop("cone", q.x, q.z, 0, false);
    q = F.P(30.0f, 3.0f); prop("lixeira", q.x, q.z, 0, true, 0.4f, 1.0f);
    q = F.P(4.0f, 4.5f); parkedCar(1, 2, q.x, q.z, 1.57f);
    q = F.P(4.0f, 7.5f); parkedCar(0, 2, q.x, q.z, 1.57f);
    w_.npcs.push_back({"mecanico", 2, w_.workshopMechanic, -1.57f});
    w_.poiList.push_back(F.P(14.0f, -1.5f, kH));
  }

  // ---- beach and sea ------------------------------------------------------------------------------------
  // Coast-local frame: s runs along the shore, t goes from the coast road edge toward the sea.
  Vec3 coastP(float s, float t, float y = 0) const {
    float x0 = xs_.front() - hwx_.front(), x1 = xs_.back() + hwx_.back();
    float z0 = zs_.front() - hwz_.front(), z1 = zs_.back() + hwz_.back();
    switch (w_.coastSide) {
      case 0: return {s, y, z0 - t};
      case 1: return {x1 + t, y, s};
      case 2: return {s, y, z1 + t};
      default: return {x0 - t, y, s};
    }
  }
  RectF coastR(float s0, float t0, float s1, float t1) const {
    Vec3 a = coastP(s0, t0), b = coastP(s1, t1);
    return {std::min(a.x, b.x), std::min(a.z, b.z), std::max(a.x, b.x), std::max(a.z, b.z)};
  }

  float seaYaw() const { switch (w_.coastSide) { case 0: return kPi; case 1: return kPi * 0.5f; case 2: return 0.0f; default: return -kPi * 0.5f; } }

  void beachAndSea() {
    if (w_.coastSide < 0) return;
    bool alongX = w_.coastSide == 0 || w_.coastSide == 2;
    float sA = alongX ? w_.beach.x0 : w_.beach.z0, sB = alongX ? w_.beach.x1 : w_.beach.z1;
    const float prom = 7.0f;
    // calcadao: wavy pedra portuguesa promenade with palms, lamps, benches and kiosks
    RectF pr = coastR(sA, 0.0f, sB, prom);
    ground(pr, kH, mat::pedra_port, 3.5f);
    w_.walkable.push_back(pr);
    w_.mapPlaza.push_back(pr);
    // kerb wall between promenade and sand
    {
      RectF kb = coastR(sA, prom - 0.25f, sB, prom);
      MeshBuilder b = mb(kb.cx(), kb.cz(), {0.85f, 0.85f, 0.82f});
      b.box(AABB({kb.x0, 0, kb.z0}, {kb.x1, kH + 0.25f, kb.z1}), mat::concrete, mat::concrete, 2.0f);
    }
    const float sy = seaYaw();
    for (float s = sA + 6.0f; s < sB - 4.0f; s += 18.0f) {
      Vec3 p = coastP(s, 1.6f);
      tree(p.x, p.z, 1, rng_.chance(0.7f) ? 3 : 4);
      Vec3 l = coastP(s + 9.0f, 0.6f);
      lampAt(l.x, l.z, w_.coastSide == 0 ? N : (w_.coastSide == 1 ? E : (w_.coastSide == 2 ? S : W)), true, 100 + (int)(s / 400));
      Vec3 bnc = coastP(s + 4.5f, prom - 1.2f);
      prop("banco", bnc.x, bnc.z, sy, true, 0.8f, 0.9f, 1);
      Vec3 bin = coastP(s + 7.0f, prom - 1.0f);
      prop("lixeira", bin.x, bin.z, 0, true, 0.3f, 1.0f);
      if (rng_.chance(0.35f)) { Vec3 fb = coastP(s + 12.0f, 2.2f); prop("canteiro", fb.x, fb.z, sy + kPi * 0.5f, true, 0.9f, 0.4f, 26); }
      if (rng_.chance(0.3f)) { Vec3 bk = coastP(s + 2.0f, prom - 0.9f); decor(DecorKind::Prop, "prop_bike", 8, {bk.x, kH, bk.z}, sy + kPi * 0.5f, 1.0f, false, 0.5f, 1.0f, -1, 23); }
    }
    // sand: slopes gently down to the water line
    float sandFrom = prom, sandTo = beachDepth_;
    RectF sand = coastR(sA, sandFrom, sB, sandTo);
    ground(sand, 0.06f, mat::sand, 4.0f, {1.0f, 0.98f, 0.95f});
    w_.walkable.push_back(sand);
    w_.mapSand.push_back(sand);
    // wet sand band at the water line
    RectF wet = coastR(sA, sandTo - 3.0f, sB, sandTo);
    ground(wet, 0.065f, mat::sand, 4.0f, {0.72f, 0.68f, 0.62f}, false);
    // coastal vegetation on the back of the sand: coconut palms, beach almonds and low shrubs
    for (float s = sA + 4.0f; s < sB - 4.0f; s += rng_.range(7.0f, 13.0f)) {
      Vec3 v = coastP(s, prom + rng_.range(1.5f, 4.5f));
      float r = rng_.uni();
      if (r < 0.4f) tree(v.x, v.z, 1, 3);
      else if (r < 0.65f) tree(v.x, v.z, 0, 6);
      else tree(v.x, v.z, 2);
    }
    // beach furniture: parasol clusters with chairs and towels, surfboards, volleyball nets, lifeguard towers, kiosks
    for (float s = sA + 10.0f; s < sB - 10.0f; s += rng_.range(6.0f, 11.0f)) {
      float t = rng_.range(prom + 6.0f, sandTo - 8.0f);
      Vec3 p = coastP(s, t);
      decor(DecorKind::Prop, "prop_guardasol", 8, {p.x, 0.06f, p.z}, rng_.range(0, kTau), 1.0f, true, 0.12f, 2.4f, -1, 7);
      int nch = rng_.irange(1, 3);
      for (int c = 0; c < nch; ++c) {
        Vec3 q = coastP(s - 0.9f + c * 1.2f, t + rng_.range(1.0f, 1.8f));
        decor(DecorKind::Prop, "prop_cadeira", 8, {q.x, 0.06f, q.z}, sy + rng_.range(-0.5f, 0.5f), 1.0f, false, 0.3f, 0.8f, -1, 8);
      }
      if (rng_.chance(0.7f)) {
        Vec3 q = coastP(s + rng_.range(1.5f, 2.5f), t + rng_.range(-0.5f, 1.2f));
        decor(DecorKind::Prop, "prop_toalha", 8, {q.x, 0.06f, q.z}, rng_.range(0, kTau), 1.0f, false, 0.4f, 0.1f, -1, 24);
      }
      if (rng_.chance(0.25f)) {
        Vec3 q = coastP(s - rng_.range(1.5f, 3.0f), t - 0.8f);
        decor(DecorKind::Prop, "prop_prancha", 8, {q.x, 0.06f, q.z}, rng_.range(0, kTau), 1.0f, false, 0.2f, 1.9f, -1, 25);
      }
    }
    for (float s = sA + 30.0f; s < sB - 20.0f; s += 70.0f) {
      Vec3 p = coastP(s, prom + 3.5f);
      decor(DecorKind::Prop, "prop_quiosque", 8, {p.x, 0.06f, p.z}, sy, 1.0f, true, 1.6f, 3.0f, -1, 9);
      w_.poiList.push_back(coastP(s, prom + 6.0f, 0.06f));
      Vec3 lg = coastP(s + 30.0f, sandTo - 10.0f);
      decor(DecorKind::Prop, "prop_salvavidas", 8, {lg.x, 0.06f, lg.z}, sy, 1.0f, true, 1.0f, 3.5f, -1, 15);
    }
    w_.poiBeach = coastP((sA + sB) * 0.5f, sandTo - 6.0f, 0.06f);
    w_.poiList.push_back(w_.poiBeach);
    w_.pickupSpots.push_back(coastP(sA + 40.0f, prom + 2.5f, 0.06f));
    // the sea: a tessellated water surface; uv.y carries the distance from the shore (waves / foam in the shader).
    // The lateral step is uniform so neighbouring strips share vertices (no cracks once the waves displace them).
    const float seaDepth = 150.0f, lat = 4.0f;
    std::vector<float> rows{0.0f};
    for (float d = 0; d < seaDepth - 1e-3f;) { d += d < 24.0f ? 3.0f : (d < 60.0f ? 6.0f : 15.0f); rows.push_back(std::min(d, seaDepth)); }
    for (float s = sA - 60.0f; s < sB + 60.0f - 1e-3f; s += lat) {
      float ss1 = std::min(s + lat, sB + 60.0f);
      for (size_t r = 0; r + 1 < rows.size(); ++r) {
        float d0 = rows[r], d1 = rows[r + 1];
        Vec3 a0 = coastP(s, sandTo + d0), b0 = coastP(ss1, sandTo + d0), c0 = coastP(ss1, sandTo + d1), e0 = coastP(s, sandTo + d1);
        a0.y = b0.y = c0.y = e0.y = w_.waterLevel;
        MeshBuilder m = mb((a0.x + c0.x) * 0.5f, (a0.z + c0.z) * 0.5f, {1, 1, 1});
        Vec2 ua{s / 6.0f, d0}, ub{ss1 / 6.0f, d0}, uc{ss1 / 6.0f, d1}, ud{s / 6.0f, d1};
        // winding: pick the order whose normal points up
        Vec3 n = (e0 - a0).cross(b0 - a0);
        if (n.y > 0) m.quad(a0, e0, c0, b0, ua, ud, uc, ub, mat::water);
        else m.quad(a0, b0, c0, e0, ua, ub, uc, ud, mat::water);
      }
    }
    // the sea floor near the shore (seen through shallow water): darker sand sloping down
    RectF floor = coastR(sA - 60.0f, sandTo, sB + 60.0f, sandTo + 14.0f);
    (void)floor;
    w_.mapWater.push_back(w_.sea);
  }

  // ---- street furniture ------------------------------------------------------------------------------
  struct PoleRef { float x, z, cx, cz; int edge; };   // cx,cz = direction of the crossarm (along the street)
  std::vector<PoleRef> poles_;
  void lampAt(float x, float z, Side faceRoad, bool promenade = false, int edge = -1) {
    for (const RectF& r : noTree_) if (r.contains(x, z)) return;
    // concrete pole with a curved arm (3D prop model 2); the light anchor is the lamp head over the road
    float dx = 0, dz = 0;
    switch (faceRoad) { case N: dz = -1; break; case S: dz = 1; break; case W: dx = -1; break; default: dx = 1; break; }
    float yaw = std::atan2(dx, dz);
    decor(DecorKind::Prop, "prop_poste", 8, {x, w_.heightAt(x, z), z}, yaw, promenade ? 0.85f : 1.0f, true, 0.18f, 7.0f, -1, 2);
    w_.lampLights.push_back({x + dx * 1.7f, 7.0f, z + dz * 1.7f});
    poles_.push_back({x, z, std::cos(yaw), -std::sin(yaw), edge});
  }

  // Overhead service wires hanging in catenaries between consecutive poles of the same kerb.
  void wires() {
    std::map<int, std::vector<PoleRef>> byEdge;
    for (const PoleRef& p : poles_) if (p.edge >= 0) byEdge[p.edge].push_back(p);
    for (auto& [edge, v] : byEdge) {
      std::sort(v.begin(), v.end(), [](const PoleRef& a, const PoleRef& b) { return a.x + a.z < b.x + b.z; });
      for (size_t i = 0; i + 1 < v.size(); ++i) {
        const PoleRef &a = v[i], &c = v[i + 1];
        float d = std::sqrt((c.x - a.x) * (c.x - a.x) + (c.z - a.z) * (c.z - a.z));
        if (d > 40.0f || d < 4.0f) continue;
        MeshBuilder b = mb((a.x + c.x) * 0.5f, (a.z + c.z) * 0.5f, {0.07f, 0.07f, 0.08f});
        for (float off : {-0.7f, 0.0f, 0.7f}) {
          Vec3 p0{a.x + a.cx * off, 6.62f, a.z + a.cz * off}, p1{c.x + c.cx * off, 6.62f, c.z + c.cz * off};
          float sag = 0.017f * d + (off == 0.0f ? 0.1f : 0.0f);
          Vec3 prev = p0;
          for (int k = 1; k <= 6; ++k) {
            float t = k / 6.0f;
            Vec3 q = lerp(p0, p1, t);
            q.y -= sag * 4.0f * t * (1.0f - t);
            b.frustum(prev, q, 0.02f, 0.02f, 3, mat::metal, false, false);
            prev = q;
          }
        }
      }
    }
  }

  // Traffic lights / stop signs, mailboxes and bus stops around every junction, plus billboards on the big avenues.
  void junctionProps() {
    for (size_t i = 0; i < xs_.size(); ++i)
      for (size_t j = 0; j < zs_.size(); ++j) {
        float cx = xs_[i], cz = zs_[j], hx = hwx_[i], hz = hwz_[j];
        bool big = avX_[i] || avZ_[j];
        for (int c = 0; c < 4; ++c) {
          float sx = (c & 1) ? 1.0f : -1.0f, sz = (c & 2) ? 1.0f : -1.0f;
          float px = cx + sx * (hx + 0.7f), pz = cz + sz * (hz + 0.7f);
          bool inside = px < w_.land.x0 + 2 || px > w_.land.x1 - 2 || pz < w_.land.z0 + 2 || pz > w_.land.z1 - 2;
          if (inside) continue;
          bool blocked = false;
          for (const RectF& r : noTree_) if (r.contains(px, pz)) blocked = true;
          if (blocked) continue;
          float yaw = std::atan2(-sx, -sz);
          if (big && (c == 0 || c == 3)) prop("semaforo", px, pz, yaw, true, 0.1f, 3.6f, 16);
          else if (!big && (c == 1 || c == 2)) prop("pare", px, pz, yaw, true, 0.08f, 2.6f, 20);
          else if (rng_.chance(0.35f)) prop(rng_.chance(0.5f) ? "correio" : "hidrante", px + sx * 0.8f, pz + sz * 0.8f, yaw, true, 0.25f, 1.1f);
        }
      }
  }

  void blockExtras(const RectF& B, District d) {
    // bus stop on the avenues / commercial streets, billboards, newsstands, bikes and carts
    if ((d == District::Centro || d == District::Comercial || d == District::Orla) && rng_.chance(0.5f)) {
      bool north = rng_.chance(0.5f);
      float x = B.x0 + B.w() * rng_.range(0.3f, 0.7f);
      float z = north ? B.z0 + 1.5f : B.z1 - 1.5f;
      bool ok = true;
      for (const RectF& r : noTree_) if (r.contains(x, z)) ok = false;
      if (ok) {
        prop("onibus", x, z, north ? kPi : 0.0f, true, 1.5f, 2.5f, 18);
        w_.poiList.push_back({x, kH, z});
      }
    }
    if (rng_.chance(0.3f)) {
      float x = B.x0 + B.w() * rng_.range(0.2f, 0.8f), z = B.z1 - 1.3f;
      bool ok = true;
      for (const RectF& r : noTree_) if (r.contains(x, z)) ok = false;
      if (ok) prop("banca", x, z, 0.0f, true, 1.3f, 2.4f, 22);
    }
    if (d == District::Industrial || d == District::Comercial) {
      float x = B.x0 + B.w() * rng_.range(0.25f, 0.75f);
      prop("outdoor", x, B.z0 + 1.0f, 0.0f, true, 2.0f, 6.0f, 19);
    }
    for (int k = 0; k < 2; ++k)
      if (rng_.chance(0.25f)) prop("bicicleta", B.x0 + rng_.range(5, B.w() - 5), B.z0 + 2.0f, rng_.range(0, kTau), false, 0.4f, 1.0f, 23);
    if (rng_.chance(0.2f)) prop("carrinho", B.x1 - 1.5f, B.z0 + rng_.range(6, B.h() - 6), rng_.range(0, kTau), false, 0.4f, 0.9f, 21);
  }

  bool inFurniture_ = false;
  void furniture() {
    inFurniture_ = true;
    auto treeOk = [&](float x, float z) {
      for (const RectF& r : noTree_) if (r.contains(x, z)) return false;
      return true;
    };
    int blockIdx = 0;
    for (auto& [B, dist] : w_.blocks) {
      ++blockIdx;
      auto along = [&](float a, float b, const std::function<void(float)>& fn, float step, float off) {
        for (float t = a + off; t < b - 2; t += step) fn(t);
      };
      const float lampOff = 0.45f;
      along(B.x0 + 3, B.x1 - 3, [&](float x) { lampAt(x, B.z0 + lampOff, N, false, blockIdx * 4); }, 24.0f, 4.0f);
      along(B.x0 + 3, B.x1 - 3, [&](float x) { lampAt(x + 7, B.z1 - lampOff, S, false, blockIdx * 4 + 1); }, 24.0f, 4.0f);
      along(B.z0 + 3, B.z1 - 3, [&](float z) { lampAt(B.x0 + lampOff, z + 3, W, false, blockIdx * 4 + 2); }, 24.0f, 5.0f);
      along(B.z0 + 3, B.z1 - 3, [&](float z) { lampAt(B.x1 - lampOff, z + 9, E, false, blockIdx * 4 + 3); }, 24.0f, 5.0f);
      float step = dist == District::Residencial ? 14.0f : 18.0f;
      along(B.x0 + 3, B.x1 - 3, [&](float x) { if (treeOk(x, B.z0 + 1.6f)) tree(x, B.z0 + 1.6f, rng_.chance(0.2f) ? 1 : 0); }, step, 9.0f);
      along(B.x0 + 3, B.x1 - 3, [&](float x) { if (treeOk(x, B.z1 - 1.6f)) tree(x, B.z1 - 1.6f, rng_.chance(0.2f) ? 1 : 0); }, step, 2.0f);
      along(B.z0 + 3, B.z1 - 3, [&](float z) { if (treeOk(B.x0 + 1.6f, z)) tree(B.x0 + 1.6f, z, 0); }, step + 2, 8.0f);
      along(B.z0 + 3, B.z1 - 3, [&](float z) { if (treeOk(B.x1 - 1.6f, z)) tree(B.x1 - 1.6f, z, 0); }, step + 2, 3.0f);
      // occasional bins / hydrants / phone booths on the pavement
      if (rng_.chance(0.5f)) prop("lixeira", B.x0 + rng_.range(6, B.w() - 6), B.z0 + 1.0f, 0, true, 0.35f, 1.0f);
      if (rng_.chance(0.3f)) prop("hidrante", B.x1 - 1.0f, B.z0 + rng_.range(6, B.h() - 6), 0, true, 0.2f, 0.8f);
      if (rng_.chance(0.2f)) prop("orelhao", B.x0 + 1.0f, B.z1 - rng_.range(6, B.h() - 6), kPi * 0.5f, true, 0.4f, 2.0f);
      blockExtras(B, dist);
      // weapon pickup spots on quieter pavements
      if (rng_.chance(0.35f)) w_.pickupSpots.push_back({B.x0 + rng_.range(8, B.w() - 8), kH, B.z1 - 1.2f});
    }
    junctionProps();
    wires();
    inFurniture_ = false;
  }

  void streetsParked() {
    // parallel parking along the kerbs (skip intersections, specials and the coast road)
    for (const RoadLine& rl : w_.roads) {
      if (rl.avenue && rng_.chance(0.5f)) continue;
      for (float t = rl.a + 12.0f; t < rl.b - 12.0f; t += rng_.range(14.0f, 30.0f)) {
        if (nearIntersection(t, 9.0f, rl.horizontal)) continue;
        bool side = rng_.chance(0.5f);
        float off = (rl.hw - 1.05f) * (side ? 1.0f : -1.0f);
        float x = rl.horizontal ? t : rl.c + off, z = rl.horizontal ? rl.c + off : t;
        bool blocked = false;
        for (const RectF& r : noTree_) if (r.inflated(4.0f).contains(x, z)) blocked = true;
        if (std::fabs(x - w_.spawnPlayer.x) < 8 && std::fabs(z - w_.spawnPlayer.z) < 8) blocked = true;
        for (int i = 0; i < 3; ++i) if ((Vec2{x, z} - Vec2{w_.vehicleSpawn[i].x, w_.vehicleSpawn[i].z}).length() < 7.0f) blocked = true;
        if (blocked) continue;
        float yaw = rl.horizontal ? (side ? kPi / 2 : -kPi / 2) : (side ? 0.0f : kPi);
        parkedCar(rng_.irange(0, 2), rng_.irange(0, 2), x, z, yaw);
      }
    }
  }

  void boundary() {
    const RectF& P = w_.playArea;
    float t = 1.0f;
    collider(AABB({P.x0 - t, 0, P.z0 - t}, {P.x1 + t, 6, P.z0}), ColKind::Wall);
    collider(AABB({P.x0 - t, 0, P.z1}, {P.x1 + t, 6, P.z1 + t}), ColKind::Wall);
    collider(AABB({P.x0 - t, 0, P.z0}, {P.x0, 6, P.z1}), ColKind::Wall);
    collider(AABB({P.x1, 0, P.z0}, {P.x1 + t, 6, P.z1}), ColKind::Wall);
    // backdrop: a ring of tall buildings beyond the land edge (not on the sea side)
    Rng r(seed_ * 7919u + 777);
    const int layers[4] = {mat::apt_beige, mat::apt_green, mat::apt_bands, mat::sobrado_pink};
    const RectF& Ld = w_.land;
    for (int side = 0; side < 4; ++side) {
      int coastMap[4] = {0, 2, 3, 1};   // backdrop side index -> coast side code (0 N(-z), 1 S(+z), 2 W(-x), 3 E(+x))
      if (coastMap[side] == w_.coastSide) continue;
      bool alongX = side < 2;
      float p = alongX ? Ld.x0 : Ld.z0, end = alongX ? Ld.x1 : Ld.z1;
      while (p < end) {
        float wd = r.range(14.0f, 22.0f), dep = r.range(12.0f, 18.0f), h = r.range(13.0f, 30.0f);
        RectF b;
        int front;
        switch (side) {
          case 0: b = {p, Ld.z0 - dep - 0.5f, p + wd, Ld.z0 - 0.5f}; front = S; break;
          case 1: b = {p, Ld.z1 + 0.5f, p + wd, Ld.z1 + dep + 0.5f}; front = N; break;
          case 2: b = {Ld.x0 - dep - 0.5f, p, Ld.x0 - 0.5f, p + wd}; front = E; break;
          default: b = {Ld.x1 + 0.5f, p, Ld.x1 + dep + 0.5f, p + wd}; front = W; break;
        }
        wallsFor(b, h, front, layers[r.irange(0, 3)], {0.8f, 0.78f, 0.72f});
        MeshBuilder rb = mb(b.cx(), b.cz(), {0.75f, 0.75f, 0.75f});
        rb.roofRect(b.x0, b.z0, b.x1, b.z1, h, mat::roof_laje, 3.0f);
        p += wd;
      }
    }
    // ground around the land so the horizon is closed (except the sea side)
    if (w_.coastSide != 0) ground({Ld.x0 - 30, Ld.z0 - 30, Ld.x1 + 30, Ld.z0}, kH, mat::grass, 6.0f);
    if (w_.coastSide != 2) ground({Ld.x0 - 30, Ld.z1, Ld.x1 + 30, Ld.z1 + 30}, kH, mat::grass, 6.0f);
    if (w_.coastSide != 3) ground({Ld.x0 - 30, Ld.z0, Ld.x0, Ld.z1}, kH, mat::grass, 6.0f);
    if (w_.coastSide != 1) ground({Ld.x1, Ld.z0, Ld.x1 + 30, Ld.z1}, kH, mat::grass, 6.0f);
  }

  // ---- shops and interiors -------------------------------------------------------------------------------
  void addShop(ShopKind kind, const char* name, Vec3 doorOutside, int front) {
    ShopDef s;
    s.id = (int)w_.shops.size();
    s.kind = kind;
    s.name = name;
    s.door = doorOutside;
    // stock (item ids from items.h, weapon ids from combat.h); prices in centavos
    switch (kind) {
      case ShopKind::Mercado: s.stock = {{0, 1, 350}, {0, 2, 600}, {0, 3, 500}, {0, 4, 250}, {0, 5, 450}, {0, 6, 400}, {0, 7, 990}, {0, 8, 2490}}; break;
      case ShopKind::Conveniencia: s.stock = {{0, 1, 450}, {0, 2, 700}, {0, 3, 650}, {0, 7, 1190}, {0, 6, 550}}; break;
      case ShopKind::Padaria: s.stock = {{0, 4, 200}, {0, 5, 400}, {0, 3, 600}, {0, 2, 650}}; break;
      case ShopKind::Ferragens: s.stock = {{1, 4, 4500}, {1, 3, 6000}, {1, 1, 3500}, {1, 2, 5000}, {0, 8, 2990}}; break;
    }
    w_.shops.push_back(s);
    pendingShopFront_.push_back(front);
  }

  void interiors() {
    for (size_t i = 0; i < w_.shops.size(); ++i) buildInterior(w_.shops[i], pendingShopFront_[i]);
  }

  void buildInterior(ShopDef& shop, int outsideFront) {
    // rooms are laid out side by side far on +X; each one a furnished, lit, walkable box
    const float RW = shop.kind == ShopKind::Mercado ? 16.0f : (shop.kind == ShopKind::Padaria ? 10.0f : 12.0f);
    const float D = shop.kind == ShopKind::Mercado ? 12.0f : 9.0f;
    const float X0 = World::kInteriorX + interiorCursor_, X1 = X0 + RW, Z0 = -D * 0.5f, Z1 = D * 0.5f, HGT = 3.2f;
    interiorCursor_ += RW + 30.0f;
    InteriorDef in;
    in.bounds = {X0, Z0, X1, Z1};
    in.height = HGT;
    in.spawn = {(X0 + X1) * 0.5f, 0, Z1 - 1.4f};
    in.spawnYaw = 0;
    in.shop = shop.id;
    int iid = (int)w_.interiors.size();
    w_.interiors.push_back(in);
    shop.interior = iid;
    const float cx = (X0 + X1) * 0.5f;
    Vec3 wallTint, bandTint;
    int floorLayer = mat::tile_floor;
    switch (shop.kind) {
      case ShopKind::Mercado: wallTint = {0.95f, 0.92f, 0.84f}; bandTint = {0.78f, 0.12f, 0.1f}; break;
      case ShopKind::Conveniencia: wallTint = {0.9f, 0.92f, 0.95f}; bandTint = {0.12f, 0.32f, 0.7f}; floorLayer = mat::tile_floor; break;
      case ShopKind::Padaria: wallTint = {0.97f, 0.9f, 0.78f}; bandTint = {0.2f, 0.45f, 0.25f}; floorLayer = mat::wood; break;
      default: wallTint = {0.8f, 0.8f, 0.78f}; bandTint = {0.85f, 0.45f, 0.08f}; floorLayer = mat::garage_floor; break;
    }
    MeshBuilder b = mb(cx, 0, {1, 1, 1});
    b.groundRect(X0, Z0, X1, Z1, 0, floorLayer, 2.0f);
    b.setTint(wallTint);
    b.wall(X1, Z1, X0, Z1, 0, HGT, mat::wall_paint, 0, (X1 - X0) / 4.0f, 0, HGT / 4.0f, 0.82f, 1.0f);
    b.wall(X0, Z0, X1, Z0, 0, HGT, mat::wall_paint, 0, (X1 - X0) / 4.0f, 0, HGT / 4.0f, 0.82f, 1.0f);
    b.wall(X0, Z1, X0, Z0, 0, HGT, mat::wall_paint, 0, (Z1 - Z0) / 4.0f, 0, HGT / 4.0f, 0.82f, 1.0f);
    b.wall(X1, Z0, X1, Z1, 0, HGT, mat::wall_paint, 0, (Z1 - Z0) / 4.0f, 0, HGT / 4.0f, 0.82f, 1.0f);
    b.setTint(bandTint);
    b.wall(X1, Z1 - 0.01f, X0, Z1 - 0.01f, 1.9f, 2.25f, mat::white, 0, 1, 0, 1, 1, 1);
    b.wall(X0, Z0 + 0.01f, X1, Z0 + 0.01f, 1.9f, 2.25f, mat::white, 0, 1, 0, 1, 1, 1);
    b.wall(X0 + 0.01f, Z1, X0 + 0.01f, Z0, 1.9f, 2.25f, mat::white, 0, 1, 0, 1, 1, 1);
    b.wall(X1 - 0.01f, Z0, X1 - 0.01f, Z1, 1.9f, 2.25f, mat::white, 0, 1, 0, 1, 1, 1);
    // skirting
    b.setTint({0.3f, 0.3f, 0.32f});
    b.wall(X0, Z0 + 0.02f, X1, Z0 + 0.02f, 0, 0.12f, mat::wall_dark, 0, 1, 0, 1, 1, 1);
    collider(AABB({X0 - 1, 0, Z0 - 1}, {X1 + 1, HGT, Z0}), ColKind::Wall);
    collider(AABB({X0 - 1, 0, Z1}, {X1 + 1, HGT, Z1 + 1}), ColKind::Wall);
    collider(AABB({X0 - 1, 0, Z0}, {X0, HGT, Z1}), ColKind::Wall);
    collider(AABB({X1, 0, Z0}, {X1 + 1, HGT, Z1}), ColKind::Wall);
    auto furn = [&](float x0, float z0, float x1, float z1, float y1, int side, int top, Vec3 tint) {
      MeshBuilder bb = mb(cx, 0, tint);
      bb.box(AABB({x0, 0, z0}, {x1, y1, z1}), side, top, 2.0f);
      collider(AABB({x0, 0, z0}, {x1, y1, z1}), ColKind::Prop);
      w_.interiorBlockers.push_back({x0, z0, x1, z1});
    };
    int pid = (int)w_.products.size();
    auto product = [&](int item, float x, float z) { w_.products.push_back({pid++, item, {x, 0, z}, shop.id}); };
    std::vector<int> itemsOnShelves;
    for (const ShopStock& st : shop.stock) if (st.kind == 0) itemsOnShelves.push_back(st.id);
    // counter + cash register near the back-right, clerk behind it
    float ctrX0 = X1 - 4.4f, ctrX1 = X1 - 1.2f, ctrZ0 = Z0 + 1.0f, ctrZ1 = Z0 + 2.1f;
    furn(ctrX0, ctrZ0, ctrX1, ctrZ1, 1.0f, mat::wood, mat::wood, {0.9f, 0.8f, 0.7f});
    {
      MeshBuilder reg = mb(cx, 0, {0.15f, 0.15f, 0.17f});
      reg.box(AABB({ctrX0 + 0.4f, 1.0f, ctrZ0 + 0.3f}, {ctrX0 + 0.9f, 1.25f, ctrZ0 + 0.7f}), mat::metal, mat::metal, 1.0f);
    }
    shop.clerk = {(ctrX0 + ctrX1) * 0.5f, 0, Z0 + 0.55f};
    const char* clerkArch = shop.kind == ShopKind::Ferragens ? "mecanico" : (shop.kind == ShopKind::Padaria ? "pedestre_mulher_npc" : "atendente");
    NpcSpawn cs{clerkArch, 4, shop.clerk, kPi, true, shop.id};
    if (shop.kind == ShopKind::Padaria) cs.archetype = "mulher_vestido";
    w_.npcs.push_back(cs);
    switch (shop.kind) {
      case ShopKind::Mercado:
      case ShopKind::Conveniencia: {
        // gondolas with product shelves + fridges on the west wall
        float g0 = X0 + 2.0f, g1 = X1 - 5.5f;
        int k = 0;
        for (float gz : {Z0 + 3.6f, Z0 + 3.6f + 3.2f}) {
          if (gz > Z1 - 2.5f) break;
          furn(g0, gz - 0.35f, g1, gz + 0.35f, 1.75f, mat::shelf, mat::white, {1, 1, 1});
          for (float x = g0 + 1.0f; x < g1 - 0.5f && k < (int)itemsOnShelves.size(); x += 2.4f) product(itemsOnShelves[k++], x, gz + 0.75f);
        }
        furn(X0, Z0 + 0.5f, X0 + 0.9f, Z1 - 2.5f, 2.0f, mat::metal, mat::white, {0.85f, 0.92f, 1.0f});
        while (k < (int)itemsOnShelves.size()) product(itemsOnShelves[k++], X0 + 1.4f, Z0 + 1.0f + k * 1.2f);
        break;
      }
      case ShopKind::Padaria: {
        // bread display cases along the counter, wooden shelves with loaves on the back wall
        furn(X0 + 1.0f, Z0 + 1.0f, ctrX0 - 0.4f, Z0 + 2.0f, 1.1f, mat::wood, mat::shelf, {1.0f, 0.95f, 0.85f});
        furn(X0 + 0.2f, Z0 + 0.2f, X0 + 6.0f, Z0 + 0.6f, 2.2f, mat::shelf, mat::wood, {1, 1, 1});
        int k = 0;
        for (float x = X0 + 1.6f; x < ctrX0 - 0.6f && k < (int)itemsOnShelves.size(); x += 1.6f) product(itemsOnShelves[k++], x, Z0 + 2.5f);
        // tables and stools
        for (float x : {X0 + 2.0f, X0 + 5.0f}) furn(x - 0.4f, Z1 - 3.0f, x + 0.4f, Z1 - 2.2f, 0.75f, mat::wood, mat::wood, {0.75f, 0.6f, 0.45f});
        break;
      }
      default: {
        // tools on wall racks, a paint shelf and the counter
        furn(X0 + 0.2f, Z0 + 0.2f, X0 + 0.7f, Z1 - 2.0f, 2.4f, mat::metal, mat::metal, {0.75f, 0.75f, 0.78f});
        furn(X0 + 3.0f, Z0 + 3.0f, X1 - 5.0f, Z0 + 3.7f, 1.6f, mat::shelf, mat::metal, {0.9f, 0.9f, 0.9f});
        furn(X0 + 3.0f, Z0 + 5.6f, X1 - 5.0f, Z0 + 6.3f, 1.6f, mat::shelf, mat::metal, {0.9f, 0.9f, 0.9f});
        int k = 0;
        for (float x = X0 + 3.8f; x < X1 - 5.5f && k < (int)itemsOnShelves.size(); x += 2.0f) product(itemsOnShelves[k++], x, Z0 + 4.4f);
        break;
      }
    }
    // door in the south wall (visual) + the pair of doors
    MeshBuilder d = mb(cx, 0, {0.5f, 0.55f, 0.6f});
    d.wall(cx + 1.0f, Z1 - 0.02f, cx - 1.0f, Z1 - 0.02f, 0, 2.4f, mat::metal, 0, 1, 0, 1, 1, 1);
    w_.interiorWalkable.push_back({X0 + 0.5f, Z0 + 0.5f, X1 - 0.5f, Z1 - 0.6f});
    int outId = (int)w_.doors.size(), inId = outId + 1;
    DoorDef out;
    out.id = outId; out.pos = shop.door; out.radius = 2.0f; out.targetDoor = inId;
    out.arrive = {cx, 0, Z1 - 1.6f}; out.arriveYaw = 0; out.label = "Entrar: " + shop.name; out.toInterior = true; out.shop = shop.id;
    DoorDef ex;
    ex.id = inId; ex.pos = {cx, 0, Z1 - 0.8f}; ex.radius = 1.8f; ex.targetDoor = outId;
    // arrive just outside, facing away from the door
    Vec3 arrive = shop.door;
    float ay = 0;
    switch (outsideFront) {
      case N: arrive.z -= 1.2f; ay = 0; break;
      case S: arrive.z += 1.2f; ay = kPi; break;
      case W: arrive.x -= 1.2f; ay = -kPi * 0.5f; break;
      default: arrive.x += 1.2f; ay = kPi * 0.5f; break;
    }
    ex.arrive = arrive; ex.arriveYaw = ay; ex.label = "Sair"; ex.toInterior = false; ex.shop = shop.id;
    w_.doors.push_back(out);
    w_.doors.push_back(ex);
    // ceiling with light panels (drawn when the camera is below it) + interior lights
    MeshBuilder cb(&w_.interiorCeiling);
    cb.setTint({0.92f, 0.92f, 0.9f});
    cb.ceiling(X0, Z0, X1, Z1, HGT, mat::wall_paint, 4.0f);
    cb.setTint({1.0f, 1.0f, 0.95f});
    for (float lx = X0 + 2.5f; lx < X1 - 1.0f; lx += 4.0f)
      for (float lz = Z0 + 2.0f; lz < Z1 - 1.0f; lz += 3.0f) {
        cb.ceiling(lx - 0.6f, lz - 0.2f, lx + 0.6f, lz + 0.2f, HGT - 0.01f, mat::white, 1.0f);
        w_.interiorLights.push_back({lx, HGT - 0.2f, lz});
      }
  }

  // ---- districts & specials -------------------------------------------------------------------------------
  void blocks() {
    int nx = (int)xs_.size() - 1, nz = (int)zs_.size() - 1;
    struct Bk { RectF r; int i, j; District d; int special = 0; };
    std::vector<Bk> bks;
    float cxm = 0, czm = 0;
    for (int i = 0; i < nx; ++i)
      for (int j = 0; j < nz; ++j) {
        Bk b;
        b.r = {xs_[i] + hwx_[i], zs_[j] + hwz_[j], xs_[i + 1] - hwx_[i + 1], zs_[j + 1] - hwz_[j + 1]};
        b.i = i; b.j = j;
        bks.push_back(b);
      }
    // district map: centre near the middle, seafront next to the coast, parks/industry sprinkled by the seed
    for (Bk& b : bks) {
      float dx = (b.i + 0.5f) / nx - 0.5f, dz = (b.j + 0.5f) / nz - 0.5f;
      float dc = std::sqrt(dx * dx + dz * dz);
      bool coastal = (w_.coastSide == 0 && b.j == 0) || (w_.coastSide == 2 && b.j == nz - 1) || (w_.coastSide == 3 && b.i == 0) ||
                     (w_.coastSide == 1 && b.i == nx - 1);
      if (coastal) b.d = District::Orla;
      else if (dc < 0.22f) b.d = District::Centro;
      else if (dc < 0.36f && rng_.chance(0.5f)) b.d = District::Comercial;
      else b.d = District::Residencial;
    }
    // specials: gas station, market, workshop, 1-2 parks, a parking lot, maybe an industrial block
    auto pick = [&](std::function<float(const Bk&)> score) {
      int best = -1;
      float bs = -1e9f;
      for (size_t k = 0; k < bks.size(); ++k) {
        if (bks[k].special) continue;
        if (bks[k].r.w() < 40.0f || bks[k].r.h() < 38.0f) continue;
        float s = score(bks[k]) + rng_.range(0.0f, 0.6f);
        if (s > bs) { bs = s; best = (int)k; }
      }
      return best;
    };
    auto centrality = [&](const Bk& b) { float dx = (b.i + 0.5f) / nx - 0.5f, dz = (b.j + 0.5f) / nz - 0.5f; return -std::sqrt(dx * dx + dz * dz); };
    int park = pick([&](const Bk& b) { return centrality(b) * 2.0f; });
    if (park >= 0) bks[park].special = 4;
    int mk = pick([&](const Bk& b) { return centrality(b) * 3.0f + (b.d == District::Orla ? -1.0f : 0.0f); });
    if (mk >= 0) bks[mk].special = 2;
    int gs = pick([&](const Bk& b) { return (avZ_[b.j] || avZ_[b.j + 1] ? 1.5f : 0.0f) + centrality(b); });
    if (gs >= 0) bks[gs].special = 1;
    int ws = pick([&](const Bk& b) { return -centrality(b) * 2.0f + (b.d == District::Orla ? -2.0f : 0.0f); });
    if (ws >= 0) bks[ws].special = 3;
    if (rng_.chance(0.7f)) { int p2 = pick([&](const Bk& b) { return -centrality(b); }); if (p2 >= 0) bks[p2].special = 4; }
    int pk = pick([&](const Bk& b) { return centrality(b) * 2.0f; });
    if (pk >= 0) bks[pk].special = 5;
    if (rng_.chance(0.6f)) { int ind = pick([&](const Bk& b) { return -centrality(b) * 3.0f + (b.d == District::Orla ? -3.0f : 0.0f); }); if (ind >= 0) bks[ind].special = 6; }
    // padaria and ferragens: ground-floor shop units on centre / commercial blocks
    int padaria = -1, ferragens = -1;
    for (int pass = 0; pass < 2; ++pass) {
      int best = -1;
      float bs = -1e9f;
      for (size_t k = 0; k < bks.size(); ++k) {
        if (bks[k].special || (int)k == padaria) continue;
        float s = (bks[k].d == District::Centro ? 2.0f : (bks[k].d == District::Comercial ? 1.5f : 0.0f)) + centrality(bks[k]) + rng_.range(0.0f, 0.5f);
        if (s > bs) { bs = s; best = (int)k; }
      }
      if (pass == 0) padaria = best; else ferragens = best;
    }
    for (Bk& b : bks) {
      int k = (int)(&b - &bks[0]);
      bool frontS = b.j < nz / 2;   // specials face the street toward the city centre... and the south street of northern blocks
      switch (b.special) {
        case 1: gasStation(b.r, frontS); break;
        case 2: market(b.r, frontS); break;
        case 3: workshop(b.r, frontS); break;
        case 4: plaza(b.r); break;
        case 5: blockParking(b.r); break;
        case 6: blockIndustrial(b.r); b.d = District::Industrial; break;
        default: {
          int tall = b.d == District::Centro ? 2 : (b.d == District::Orla ? 3 : (b.d == District::Comercial ? 1 : 0));
          if (k == padaria || k == ferragens) {
            // reserve a frontage plot for the shop on the north street, then fill the block normally
            sidewalkBands(b.r, true, true, true, true);
            RectF L = lotOf(b.r);
            RectF shopLot{L.cx() - 6.0f, L.z0, L.cx() + 6.0f, L.z0 + 9.0f};
            ground(L, kH, mat::concrete, 4.0f, {0.88f, 0.88f, 0.86f});
            if (k == padaria) shopUnit(shopLot, N, mat::shop_padaria, ShopKind::Padaria, "Padaria Pão Dourado");
            else shopUnit(shopLot, N, mat::shop_ferragens, ShopKind::Ferragens, "Ferragens São Jorge");
            plotRow(N, L.x0, shopLot.x0, L.z0, 13.0f, std::max(1, tall), false);
            plotRow(N, shopLot.x1, L.x1, L.z0, 13.0f, std::max(1, tall), false);
            plotRow(S, L.x0, L.x1, L.z1, 13.0f, tall, true);
            float zA = L.z0 + 14.0f, zB = L.z1 - 14.0f;
            if (zB - zA > 6) { plotRow(W, zA, zB, L.x0, 11.0f, 1, true); plotRow(E, zA, zB, L.x1, 11.0f, 1, true); }
            noTree_.push_back(shopLot.inflated(3.0f));
          } else {
            float dx = (b.i + 0.5f) / nx - 0.5f, dz = (b.j + 0.5f) / nz - 0.5f;
            periphery_ = b.d == District::Residencial && std::sqrt(dx * dx + dz * dz) > 0.34f;
            blockCentre(b.r, tall);
            periphery_ = false;
          }
          break;
        }
      }
      w_.blocks.push_back({b.r, b.special == 4 ? District::Parque : b.d});
    }
  }

  void population() {
    // player start: on the pavement next to the main park (or the first residential block)
    Vec3 base = w_.poiPlaza;
    if (base.x == 0 && base.z == 0 && !w_.blocks.empty()) base = {w_.blocks[0].first.cx(), kH, w_.blocks[0].first.z1 - 1.2f};
    // the nearest north-facing pavement point of the park block
    RectF pb = w_.blocks[0].first;
    for (auto& [r, d] : w_.blocks) if (r.contains(base.x, base.z)) pb = r;
    w_.spawnPlayer = {pb.cx() - 4.0f, kH, pb.z0 + 1.0f};
    w_.spawnYaw = kPi;
    // the player's car at the kerb of the street just north of that block
    const RoadLine* north = nullptr;
    for (const RoadLine& rl : w_.roads)
      if (rl.horizontal && std::fabs((rl.c + rl.hw) - pb.z0) < 0.6f) north = &rl;
    float carZ = north ? north->c + north->hw - 1.4f : pb.z0 - 4.0f;
    w_.vehicleSpawn[0] = {pb.cx() + 1.0f, 0, carZ};
    w_.vehicleYaw[0] = -kPi / 2;
    w_.vehicleSpawn[1] = marketSpot_;   // a free bay of the market car park
    w_.vehicleYaw[1] = marketSpotYaw_;
    w_.vehicleSpawn[2] = {w_.workshopBayEntry.x - 6.0f, 0, w_.workshopBayEntry.z};
    w_.vehicleYaw[2] = 0;
    // pedestrians on the pavements, more where the city is denser
    static const char* arch[] = {"mulher_rosa", "homem_polo", "jovem_moletom", "mulher_vestido", "corredor", "vizinho"};
    int target = std::min(30, (int)w_.blocks.size() * 2 + 4);
    for (int k = 0; k < target; ++k) {
      const auto& [r, d] = w_.blocks[rng_.irange(0, (int)w_.blocks.size() - 1)];
      int side = rng_.irange(0, 3);
      Vec3 p;
      switch (side) {
        case 0: p = {rng_.range(r.x0 + 3, r.x1 - 3), kH, r.z0 + 1.2f}; break;
        case 1: p = {rng_.range(r.x0 + 3, r.x1 - 3), kH, r.z1 - 1.2f}; break;
        case 2: p = {r.x0 + 1.2f, kH, rng_.range(r.z0 + 3, r.z1 - 3)}; break;
        default: p = {r.x1 - 1.2f, kH, rng_.range(r.z0 + 3, r.z1 - 3)}; break;
      }
      w_.npcs.push_back({arch[k % 6], 0, p, rng_.range(0, kTau)});
    }
    // beach-goers
    if (w_.coastSide >= 0)
      for (int k = 0; k < 6; ++k) {
        Vec3 p = coastP(rng_.range((w_.coastSide % 2 == 0 ? w_.beach.x0 : w_.beach.z0) + 20, (w_.coastSide % 2 == 0 ? w_.beach.x1 : w_.beach.z1) - 20),
                        rng_.range(9.0f, beachDepth_ - 6.0f), 0.06f);
        w_.npcs.push_back({k % 2 ? "mulher_vestido" : "jovem_moletom", 0, p, rng_.range(0, kTau)});
      }
  }

  void run() {
    plan();
    blocks();
    streets();
    beachAndSea();
    furniture();
    boundary();
    interiors();
    population();
    streetsParked();
    // at least one spot per weapon pickup
    for (int k = 0; w_.pickupSpots.size() < 8 && k < 64; ++k) {
      const RectF& B = w_.blocks[rng_.irange(0, (int)w_.blocks.size() - 1)].first;
      w_.pickupSpots.push_back({rng_.range(B.x0 + 6, B.x1 - 6), kH, B.z0 + 1.2f});
    }
  }

  std::vector<RectF> noTree_;

 private:
  World& w_;
  Rng rng_;
  uint32_t seed_;
  std::map<std::pair<int, int>, int> map_;
  float beachDepth_ = 0;
  float interiorCursor_ = 0;
  bool placedNeighbour_ = false;
  bool periphery_ = false;
  Vec3 marketSpot_;
  float marketSpotYaw_ = 0;
  int sideLayer_ = mat::wall_paint;
  std::vector<int> pendingShopFront_;
};

}  // namespace

// ------------------------------------------------------------------------------------------------------------
void World::buildGrid() {
  float minX = 1e9f, minZ = 1e9f, maxX = -1e9f, maxZ = -1e9f;
  for (auto& c : colliders) {
    minX = std::min(minX, c.box.mn.x); minZ = std::min(minZ, c.box.mn.z);
    maxX = std::max(maxX, c.box.mx.x); maxZ = std::max(maxZ, c.box.mx.z);
  }
  gridMinX = (int)std::floor(minX / gridCell) - 1;
  gridMinZ = (int)std::floor(minZ / gridCell) - 1;
  gridW = (int)std::floor(maxX / gridCell) + 2 - gridMinX;
  gridH = (int)std::floor(maxZ / gridCell) + 2 - gridMinZ;
  grid.assign((size_t)gridW * gridH, {});
  for (size_t i = 0; i < colliders.size(); ++i) {
    const AABB& b = colliders[i].box;
    int x0 = (int)std::floor(b.mn.x / gridCell) - gridMinX, x1 = (int)std::floor(b.mx.x / gridCell) - gridMinX;
    int z0 = (int)std::floor(b.mn.z / gridCell) - gridMinZ, z1 = (int)std::floor(b.mx.z / gridCell) - gridMinZ;
    for (int z = std::max(0, z0); z <= std::min(gridH - 1, z1); ++z)
      for (int x = std::max(0, x0); x <= std::min(gridW - 1, x1); ++x) grid[(size_t)z * gridW + x].push_back((int)i);
  }
}

void World::queryColliders(float x0, float z0, float x1, float z1, std::vector<int>& out) const {
  out.clear();
  if (grid.empty()) return;
  int gx0 = std::max(0, (int)std::floor(x0 / gridCell) - gridMinX), gx1 = std::min(gridW - 1, (int)std::floor(x1 / gridCell) - gridMinX);
  int gz0 = std::max(0, (int)std::floor(z0 / gridCell) - gridMinZ), gz1 = std::min(gridH - 1, (int)std::floor(z1 / gridCell) - gridMinZ);
  for (int z = gz0; z <= gz1; ++z)
    for (int x = gx0; x <= gx1; ++x)
      for (int i : grid[(size_t)z * gridW + x])
        if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
}

int World::interiorAt(float x, float z) const {
  if (x < kInteriorX - 60.0f) return -1;
  for (size_t i = 0; i < interiors.size(); ++i)
    if (interiors[i].bounds.contains(x, z)) return (int)i;
  return -1;
}

// signed distance past the shoreline (positive = into the sea)
static float seaDistance(const World& w, float x, float z) {
  switch (w.coastSide) {
    case 0: return -z - w.shoreline;
    case 1: return x - w.shoreline;
    case 2: return z - w.shoreline;
    case 3: return -x - w.shoreline;
    default: return -1e9f;
  }
}

float World::heightAt(float x, float z) const {
  if (interiorAt(x, z) >= 0) return 0.0f;
  for (const RectF& r : lowRects)
    if (r.contains(x, z)) return 0.0f;
  if (coastSide >= 0) {
    float d = seaDistance(*this, x, z);
    if (d > -beach.w() - beach.h() && beach.contains(x, z)) {
      // sand slopes from the promenade toward the water line
      if (d > -(std::min(beach.w(), beach.h()) - 7.0f)) return clamp(0.06f - (d + 30.0f) * 0.0f, -0.2f, 0.14f) * 0.0f + 0.06f;
    }
    if (d > 0) return std::max(-6.0f, 0.06f - d * 0.09f);   // sea floor
  }
  return kSidewalkH;
}

float World::waterDepth(float x, float z) const {
  if (coastSide < 0) return 0.0f;
  float d = seaDistance(*this, x, z);
  if (d <= 0) return 0.0f;
  return waterLevel - heightAt(x, z);
}

Vec2 World::nearestRoadPoint(Vec2 p) const {
  Vec2 best = p;
  float bd = 1e9f;
  for (const RoadLine& r : roads) {
    Vec2 q = r.horizontal ? Vec2{clamp(p.x, r.a, r.b), r.c} : Vec2{r.c, clamp(p.y, r.a, r.b)};
    float d = (q - p).length();
    if (d < bd) { bd = d; best = q; }
  }
  return best;
}

std::vector<Vec2> World::roadRoute(Vec2 a, Vec2 b) const {
  // pick the closest line to each end, then go through the crossing(s) of the grid
  auto closest = [&](Vec2 p) {
    int best = -1;
    float bd = 1e9f;
    for (size_t i = 0; i < roads.size(); ++i) {
      const RoadLine& r = roads[i];
      Vec2 q = r.horizontal ? Vec2{clamp(p.x, r.a, r.b), r.c} : Vec2{r.c, clamp(p.y, r.a, r.b)};
      float d = (q - p).length();
      if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
  };
  int la = closest(a), lb = closest(b);
  std::vector<Vec2> out;
  if (la < 0 || lb < 0) return {b};
  const RoadLine &A = roads[la], &B = roads[lb];
  auto onLine = [&](const RoadLine& r, Vec2 p) { return r.horizontal ? Vec2{clamp(p.x, r.a, r.b), r.c} : Vec2{r.c, clamp(p.y, r.a, r.b)}; };
  Vec2 pa = onLine(A, a), pb = onLine(B, b);
  out.push_back(pa);
  if (la != lb) {
    if (A.horizontal != B.horizontal) {
      out.push_back(A.horizontal ? Vec2{B.c, A.c} : Vec2{A.c, B.c});
    } else {
      // parallel: use the perpendicular line closest to the middle of the trip
      Vec2 mid = (pa + pb) * 0.5f;
      int best = -1;
      float bd = 1e9f;
      for (size_t i = 0; i < roads.size(); ++i) {
        if (roads[i].horizontal == A.horizontal) continue;
        float d = std::fabs(roads[i].c - (A.horizontal ? mid.x : mid.y));
        if (d < bd) { bd = d; best = (int)i; }
      }
      if (best >= 0) {
        const RoadLine& C = roads[best];
        out.push_back(A.horizontal ? Vec2{C.c, A.c} : Vec2{A.c, C.c});
        out.push_back(A.horizontal ? Vec2{C.c, B.c} : Vec2{B.c, C.c});
      }
    }
  }
  out.push_back(pb);
  out.push_back(b);
  return out;
}

void buildWorld(World& w, uint32_t seed) {
  w = World();
  w.seed = seed;
  Gen g(w, seed);
  g.run();
  for (auto& c : w.chunks) c.bounds = c.mesh.bounds;
  w.buildGrid();
}

void renderMinimap(const World& w, std::vector<uint8_t>& rgba, int size, float extent) {
  rgba.assign((size_t)size * size * 4, 0);
  auto put = [&](int x, int y, float r, float g, float b) {
    size_t i = ((size_t)y * size + x) * 4;
    rgba[i] = (uint8_t)clamp(r * 255.0f, 0.0f, 255.0f); rgba[i + 1] = (uint8_t)clamp(g * 255.0f, 0.0f, 255.0f);
    rgba[i + 2] = (uint8_t)clamp(b * 255.0f, 0.0f, 255.0f); rgba[i + 3] = 255;
  };
  float k = (float)size / (2 * extent);
  // muted, dark palette (mature UI): land graphite, roads lighter, buildings warm grey, water deep teal
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) put(x, y, 0.105f, 0.115f, 0.13f);
  auto fill = [&](const RectF& r, float cr, float cg, float cb) {
    int x0 = (int)std::floor((r.x0 + extent) * k), x1 = (int)std::ceil((r.x1 + extent) * k);
    int y0 = (int)std::floor((r.z0 + extent) * k), y1 = (int)std::ceil((r.z1 + extent) * k);
    for (int y = std::max(0, y0); y < std::min(size, y1); ++y)
      for (int x = std::max(0, x0); x < std::min(size, x1); ++x) put(x, y, cr, cg, cb);
  };
  for (const RectF& r : w.mapWater) fill(r, 0.07f, 0.17f, 0.22f);
  for (const RectF& r : w.mapSand) fill(r, 0.36f, 0.33f, 0.26f);
  for (const RectF& r : w.mapGreen) fill(r, 0.14f, 0.2f, 0.16f);
  for (const RectF& r : w.mapWalk) fill(r, 0.25f, 0.27f, 0.29f);
  for (const RectF& r : w.mapParking) fill(r, 0.2f, 0.21f, 0.23f);
  for (const RectF& r : w.mapRoads) fill(r, 0.42f, 0.44f, 0.47f);
  for (const RectF& r : w.mapPlaza) fill(r, 0.33f, 0.33f, 0.31f);
  for (const RectF& r : w.mapBuildings) fill(r, 0.2f, 0.2f, 0.22f);
  for (int i = 0; i < size; ++i) { put(i, 0, 0.05f, 0.06f, 0.08f); put(i, size - 1, 0.05f, 0.06f, 0.08f); put(0, i, 0.05f, 0.06f, 0.08f); put(size - 1, i, 0.05f, 0.06f, 0.08f); }
}

}  // namespace gtabr
