#include "world.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "material_ids.h"

namespace gtabr {

namespace {

constexpr float SW = 2.5f;  // sidewalk width
constexpr float kH = World::kSidewalkH;

// Street layout: centres and half widths (metres)
const float kXs[3] = {-48, 0, 48}, kHwX[3] = {5.5f, 5.5f, 5.5f};
const float kZs[3] = {-48, 0, 48}, kHwZ[3] = {5.5f, 6.5f, 5.5f};

struct Interval { float a, b; };
const Interval kBx[4] = {{-80, -53.5f}, {-42.5f, -5.5f}, {5.5f, 42.5f}, {53.5f, 80}};
const Interval kBz[4] = {{-80, -53.5f}, {-42.5f, -6.5f}, {6.5f, 42.5f}, {53.5f, 80}};

enum Side { N = 0, E = 1, S = 2, W = 3 };

struct Facade {
  int layer;
  Vec3 sideTint;
  float height;      // wall height
  bool flat;         // flat roof (laje) vs gable tile roof
  float aspectH;     // preferred height for this facade (stretch control)
};

class Gen {
 public:
  explicit Gen(World& w) : w_(w), rng_(0xC0FFEE1234ull) {}

  // ---- infrastructure --------------------------------------------------------------------------
  int chunkIndex(float x, float z) {
    int cx = (int)std::floor(x / World::kChunk), cz = (int)std::floor(z / World::kChunk);
    auto key = std::make_pair(cx, cz);
    auto it = map_.find(key);
    if (it != map_.end()) return it->second;
    w_.chunks.emplace_back();
    w_.chunks.back().cx = cx;
    w_.chunks.back().cz = cz;
    map_[key] = (int)w_.chunks.size() - 1;
    return (int)w_.chunks.size() - 1;
  }
  MeshBuilder mb(float x, float z, Vec3 tint = {1, 1, 1}) {
    MeshBuilder b(&w_.chunks[chunkIndex(x, z)].mesh);
    b.setTint(tint);
    return b;
  }

  void ground(RectF r, float y, int layer, float tile, Vec3 tint = {1, 1, 1}) {
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

  void decor(DecorKind k, const std::string& base, int dirs, Vec3 pos, float yaw, float scale = 1.0f, bool collide = false, float colR = 0.3f, float colH = 2.0f) {
    DecorInstance d;
    d.kind = k; d.base = base; d.dirCount = dirs; d.pos = pos; d.yaw = yaw; d.scale = scale; d.mirror = rng_.chance(0.5f);
    w_.decor.push_back(d);
    if (collide) collider(AABB({pos.x - colR, pos.y, pos.z - colR}, {pos.x + colR, pos.y + colH, pos.z + colR}), k == DecorKind::Tree ? ColKind::Tree : ColKind::Prop);
  }
  void tree(float x, float z, int type = 0) {
    static const char* names[3] = {"arvore", "palmeira", "arbusto"};
    int variant = rng_.irange(0, 1);
    std::string base = std::string("tree_") + names[type] + std::to_string(variant);
    float s = type == 2 ? rng_.range(0.9f, 1.3f) : rng_.range(0.88f, 1.15f);
    decor(DecorKind::Tree, base, 4, {x, w_.heightAt(x, z), z}, rng_.range(0, kTau), s, type != 2, type == 2 ? 0.5f : 0.38f, 3.0f);
  }
  void prop(const char* name, float x, float z, float yaw, bool collide = true, float r = 0.35f, float h = 1.0f) {
    decor(DecorKind::Prop, std::string("prop_") + name, 8, {x, w_.heightAt(x, z), z}, yaw, 1.0f, collide, r, h);
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
        b.setTint(tint);
        b.wall(x0, z0, x1, z1, 0.0f, h, mat::wall_paint, 0, len / 4.0f, 0, h / 4.0f, 0.78f, 1.0f);
      }
    };
    wallSeg(S, r.x0, r.z1, r.x1, r.z1);
    wallSeg(E, r.x1, r.z1, r.x1, r.z0);
    wallSeg(N, r.x1, r.z0, r.x0, r.z0);
    wallSeg(W, r.x0, r.z0, r.x0, r.z1);
  }

  void houseBuilding(const RectF& r, int front, const Facade& f) {
    float h = f.height;
    wallsFor(r, h, front, f.layer, f.sideTint);
    collider(AABB({r.x0, 0, r.z0}, {r.x1, h + 1.5f, r.z1}), ColKind::Building);
    w_.mapBuildings.push_back(r);
    MeshBuilder b = mb(r.cx(), r.cz(), {1, 1, 1});
    float cx = r.cx(), cz = r.cz();
    if (f.flat) {
      // laje roof + parapet
      b.setTint({0.82f, 0.82f, 0.82f});
      b.roofRect(r.x0, r.z0, r.x1, r.z1, h, mat::roof_laje, 3.0f);
      b.setTint(f.sideTint);
      float p = 0.22f, ph = 0.7f;
      MeshBuilder pb = b;
      pb.box(AABB({r.x0, h, r.z0}, {r.x1, h + ph, r.z0 + p}), mat::wall_paint, mat::wall_paint, 4.0f);
      pb.box(AABB({r.x0, h, r.z1 - p}, {r.x1, h + ph, r.z1}), mat::wall_paint, mat::wall_paint, 4.0f);
      pb.box(AABB({r.x0, h, r.z0 + p}, {r.x0 + p, h + ph, r.z1 - p}), mat::wall_paint, mat::wall_paint, 4.0f);
      pb.box(AABB({r.x1 - p, h, r.z0 + p}, {r.x1, h + ph, r.z1 - p}), mat::wall_paint, mat::wall_paint, 4.0f);
      // rooftop details: water tank (caixa d'agua), AC units
      if ((r.x1 - r.x0) > 5.5f && rng_.chance(0.8f)) {
        float tx = r.x0 + rng_.range(1.3f, (r.x1 - r.x0) - 1.3f), tz = r.z0 + rng_.range(1.3f, (r.z1 - r.z0) - 1.3f);
        MeshBuilder tb = mb(tx, tz, {0.15f, 0.42f, 0.78f});
        tb.prism({tx, h, tz}, 0.85f, 1.2f, 10, mat::white);
        MeshBuilder lb = mb(tx, tz, {0.9f, 0.9f, 0.92f});
        lb.prism({tx, h + 1.2f, tz}, 0.9f, 0.1f, 10, mat::white);
      }
      if (rng_.chance(0.5f)) {
        float ax = r.x0 + rng_.range(0.8f, (r.x1 - r.x0) - 1.6f), az = r.z0 + rng_.range(0.8f, (r.z1 - r.z0) - 1.6f);
        MeshBuilder ab = mb(ax, az, {0.82f, 0.84f, 0.86f});
        ab.box(AABB({ax, h, az}, {ax + 0.9f, h + 0.55f, az + 0.45f}), mat::metal, mat::metal, 1.0f);
      }
    } else {
      bool ridgeAlongX = (front == N || front == S);
      float rise = (ridgeAlongX ? (r.z1 - r.z0) : (r.x1 - r.x0)) * 0.22f;
      b.setTint(rng_.chance(0.8f) ? Vec3{1, 1, 1} : Vec3{0.85f, 0.85f, 0.85f});
      int roofLayer = rng_.chance(0.78f) ? mat::roof_tile : mat::roof_fiber;
      float ov = 0.35f;
      b.gableRoof(r.x0, r.z0, r.x1, r.z1, h, rise, ridgeAlongX, roofLayer, 2.6f, ov);
      // gable end triangles on the two short sides
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

  Facade pickFacade(Rng& rng, int kind) {
    // kind 0 house, 1 sobrado, 2 apartment, 3 shop-house
    switch (kind) {
      case 0: {
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
      default: return {mat::apt_green, {0.45f, 0.62f, 0.45f}, 9.0f, true, 9.0f};
    }
  }

  // A row of plots along one block edge.
  //  side = which side of the block the row faces (the facade faces the street)
  //  a..b = extent along the edge, fixed = coordinate of the street-facing line, depth = building depth
  void plotRow(Side side, float a, float b, float fixed, float depth, bool allowTall = true) {
    float pos = a;
    while (b - pos > 5.0f) {
      float wpl = rng_.range(7.0f, 10.5f);
      if (b - (pos + wpl) < 6.0f) wpl = b - pos;
      float d = depth + rng_.range(-1.0f, 1.0f);
      RectF r;
      switch (side) {
        case N: r = {pos, fixed, pos + wpl, fixed + d}; break;
        case S: r = {pos, fixed - d, pos + wpl, fixed}; break;
        case W: r = {fixed, pos, fixed + d, pos + wpl}; break;
        default: r = {fixed - d, pos, fixed, pos + wpl}; break;
      }
      // the facade side is the street side, which is the opposite of the lot edge direction
      int front = (side == N) ? N : (side == S) ? S : (side == W) ? W : E;
      float roll = rng_.uni();
      int kind = roll < 0.55f ? 0 : roll < 0.72f ? 1 : (allowTall && roll < 0.92f) ? 2 : 3;
      Facade f = pickFacade(rng_, kind);
      f.height *= rng_.range(0.96f, 1.06f);
      houseBuilding(r, front, f);
      // door/garage clutter on the pavement: small bin or pot
      if (rng_.chance(0.25f)) {
        float px = (side == N || side == S) ? r.x0 + 0.6f : (side == W ? r.x0 - 0.5f : r.x1 + 0.5f);
        float pz = (side == N) ? r.z0 - 0.5f : (side == S ? r.z1 + 0.5f : r.z0 + 0.6f);
        prop(rng_.chance(0.5f) ? "lixeira" : "vaso", px, pz, rng_.range(0, kTau), true, 0.32f, 1.0f);
      }
      pos += wpl;
    }
  }

  // ---- streets ---------------------------------------------------------------------------------------
  void streets() {
    // EW streets full length
    for (int i = 0; i < 3; ++i) {
      RectF r{-World::kHalf, kZs[i] - kHwZ[i], World::kHalf, kZs[i] + kHwZ[i]};
      ground(r, 0.0f, mat::asphalt, 6.0f);
      w_.lowRects.push_back(r);
      w_.mapRoads.push_back(r);
    }
    // NS street pieces between the EW streets
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 4; ++j) {
        RectF r{kXs[i] - kHwX[i], kBz[j].a, kXs[i] + kHwX[i], kBz[j].b};
        ground(r, 0.0f, mat::asphalt, 6.0f);
        w_.lowRects.push_back(r);
        w_.mapRoads.push_back(r);
      }
    // a few cracked patches for variety
    for (int k = 0; k < 14; ++k) {
      float px = rng_.range(-70, 70), pz = rng_.range(-70, 70);
      bool onRoad = false;
      for (int i = 0; i < 3; ++i) {
        if (std::fabs(pz - kZs[i]) < kHwZ[i] - 2.5f) onRoad = true;
        if (std::fabs(px - kXs[i]) < kHwX[i] - 2.5f) onRoad = true;
      }
      if (!onRoad) continue;
      ground({px - 2.5f, pz - 2.5f, px + 2.5f, pz + 2.5f}, 0.004f, mat::asphalt_cracked, 5.0f, {0.95f, 0.95f, 0.95f});
    }
    // lane markings
    for (int i = 0; i < 3; ++i) {
      // EW centre line
      bool main = (i == 1);
      for (float x = -78; x < 78; x += 6.0f) {
        if (nearIntersection(x, 7.5f, true) || nearIntersection(x + 3.0f, 7.5f, true)) continue;
        Vec3 col = main ? Vec3{1.0f, 0.82f, 0.1f} : Vec3{0.95f, 0.95f, 0.92f};
        if (main) {
          paint({x, kZs[i] - 0.22f, x + 3.2f, kZs[i] - 0.08f}, col);
          paint({x, kZs[i] + 0.08f, x + 3.2f, kZs[i] + 0.22f}, col);
        } else {
          paint({x, kZs[i] - 0.08f, x + 3.0f, kZs[i] + 0.08f}, col);
        }
      }
      for (float z = -78; z < 78; z += 6.0f) {
        if (nearIntersection(z, 7.5f, false) || nearIntersection(z + 3.0f, 7.5f, false)) continue;
        paint({kXs[i] - 0.08f, z, kXs[i] + 0.08f, z + 3.0f}, {0.95f, 0.95f, 0.92f});
      }
    }
    // crosswalks + stop lines at every intersection arm
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) {
        float cx = kXs[i], cz = kZs[j], hx = kHwX[i], hz = kHwZ[j];
        // zebra on each of the 4 arms, just outside the intersection box
        zebra(cx, cz - hz - 2.4f, true, hx, i, j);   // north arm: stripes run along z, spread along x
        zebra(cx, cz + hz + 2.4f, true, hx, i, j);
        zebra(cx - hx - 2.4f, cz, false, hz, i, j);
        zebra(cx + hx + 2.4f, cz, false, hz, i, j);
        // stop lines
        paint({cx - hx, cz - hz - 4.6f, cx, cz - hz - 4.3f}, {0.95f, 0.95f, 0.92f});
        paint({cx, cz + hz + 4.3f, cx + hx, cz + hz + 4.6f}, {0.95f, 0.95f, 0.92f});
        paint({cx - hx - 4.6f, cz, cx - hx - 4.3f, cz + hz}, {0.95f, 0.95f, 0.92f});
        paint({cx + hx + 4.3f, cz - hz, cx + hx + 4.6f, cz}, {0.95f, 0.95f, 0.92f});
      }
  }

  bool nearIntersection(float v, float margin, bool alongX) {
    // v is the coordinate along the street being marked
    const float* cs = alongX ? kXs : kZs;
    for (int i = 0; i < 3; ++i)
      if (std::fabs(v - cs[i]) < (alongX ? kHwX[i] : kHwZ[i]) + margin) return true;
    return false;
  }

  void paint(RectF r, Vec3 col, float y = 0.012f) {
    ground(r, y, mat::white, 4.0f, col);
  }

  void zebra(float cx, float cz, bool alongX, float half, int, int) {
    // alongX: stripes are spread along x (arm runs north/south); otherwise along z
    const float stripe = 0.55f, gap = 0.55f, len = 3.0f;
    float start = -half + 0.6f;
    for (float o = start; o + stripe < half - 0.6f; o += stripe + gap) {
      if (alongX) paint({cx + o, cz - len * 0.5f, cx + o + stripe, cz + len * 0.5f}, {0.93f, 0.93f, 0.9f});
      else paint({cx - len * 0.5f, cz + o, cx + len * 0.5f, cz + o + stripe}, {0.93f, 0.93f, 0.9f});
    }
    RectF cross = alongX ? RectF{cx - half, cz - len * 0.5f, cx + half, cz + len * 0.5f} : RectF{cx - len * 0.5f, cz - half, cx + len * 0.5f, cz + half};
    w_.walkable.push_back(cross);
  }

  // ---- blocks ---------------------------------------------------------------------------------------
  void sidewalkBands(const RectF& B, bool n, bool e, bool s, bool wst) {
    float x0 = B.x0, x1 = B.x1, z0 = B.z0, z1 = B.z1;
    auto band = [&](RectF r, int curbSide) {
      ground(r, kH, mat::sidewalk, 3.0f);
      w_.walkable.push_back(r);
      w_.mapWalk.push_back(r);
      MeshBuilder b = mb(r.cx(), r.cz(), {0.78f, 0.78f, 0.78f});
      // curb face towards the road
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

  RectF lotOf(const RectF& B, bool n, bool e, bool s, bool wst) {
    return {B.x0 + (wst ? SW : 0), B.z0 + (n ? SW : 0), B.x1 - (e ? SW : 0), B.z1 - (s ? SW : 0)};
  }

  void blockGeneric(const RectF& B, bool n, bool e, bool s, bool wst, bool inner) {
    sidewalkBands(B, n, e, s, wst);
    RectF L = lotOf(B, n, e, s, wst);
    int surface = rng_.chance(0.5f) ? mat::grass : mat::dirt;
    ground(L, kH, surface, 4.0f, surface == mat::grass ? Vec3{0.85f, 0.95f, 0.8f} : Vec3{0.9f, 0.85f, 0.8f});
    w_.mapGreen.push_back(L);
    float dN = 12.0f, dS = 12.0f, dW = 11.0f, dE = 11.0f;
    float zA = L.z0 + (n ? dN : 0), zB = L.z1 - (s ? dS : 0);
    if (n) plotRow(N, L.x0, L.x1, L.z0, dN);
    if (s) plotRow(S, L.x0, L.x1, L.z1, dS);
    if (wst) plotRow(W, zA, zB, L.x0, dW, false);
    if (e) plotRow(E, zA, zB, L.x1, dE, false);
    // back-yard trees in the leftover middle
    RectF mid{L.x0 + (wst ? dW : 0) + 1.5f, zA + 1.5f, L.x1 - (e ? dE : 0) - 1.5f, zB - 1.5f};
    if (mid.x1 - mid.x0 > 3 && mid.z1 - mid.z0 > 3 && inner)
      for (int k = 0; k < 3; ++k) tree(rng_.range(mid.x0, mid.x1), rng_.range(mid.z0, mid.z1), rng_.chance(0.3f) ? 1 : 0);
    if (!inner) {
      // outer blocks: open lots with scattered trees toward the world edge
      for (int k = 0; k < 6; ++k) tree(rng_.range(L.x0 + 2, L.x1 - 2), rng_.range(L.z0 + 2, L.z1 - 2), rng_.chance(0.3f) ? 1 : (rng_.chance(0.3f) ? 2 : 0));
    }
  }

  // --- street furniture ------------------------------------------------------------------------------
  void streetLamp(float x, float z, Side faceRoad) {
    float h = 7.2f;
    pole(x, z, h, 0.09f, mat::metal, {0.35f, 0.37f, 0.4f});
    float ax = x, az = z;
    float dx = 0, dz = 0;
    switch (faceRoad) { case N: dz = -1; break; case S: dz = 1; break; case W: dx = -1; break; default: dx = 1; break; }
    ax += dx * 1.7f; az += dz * 1.7f;
    MeshBuilder b = mb(x, z, {0.35f, 0.37f, 0.4f});
    float mnx = std::min(x, ax) - 0.05f, mxx = std::max(x, ax) + 0.05f, mnz = std::min(z, az) - 0.05f, mxz = std::max(z, az) + 0.05f;
    b.box(AABB({mnx, h - 0.1f, mnz}, {mxx, h, mxz}), mat::metal, mat::metal, 1.0f);
    MeshBuilder l = mb(x, z, {1.0f, 0.95f, 0.8f});
    l.box(AABB({ax - 0.28f, h - 0.16f, az - 0.14f}, {ax + 0.28f, h - 0.08f, az + 0.14f}), mat::white, mat::white, 1.0f);
  }

  void furniture() {
    // lamps and trees along every sidewalk band of the four main block rows/columns
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j) {
        RectF B{kBx[i].a, kBz[j].a, kBx[i].b, kBz[j].b};
        bool bn = j > 0, bs = j < 3, bw = i > 0, be = i < 3;
        auto along = [&](float a, float b, std::function<void(float)> fn, float step, float off) {
          for (float t = a + off; t < b - 2; t += step) fn(t);
        };
        float lampOff = 0.45f;
        if (bn) along(B.x0 + 3, B.x1 - 3, [&](float x) { streetLamp(x, B.z0 + lampOff, N); }, 22.0f, 4.0f);
        if (bs) along(B.x0 + 3, B.x1 - 3, [&](float x) { streetLamp(x + 7, B.z1 - lampOff, S); }, 22.0f, 4.0f);
        if (bw) along(B.z0 + 3, B.z1 - 3, [&](float z) { streetLamp(B.x0 + lampOff, z + 3, W); }, 22.0f, 5.0f);
        if (be) along(B.z0 + 3, B.z1 - 3, [&](float z) { streetLamp(B.x1 - lampOff, z + 9, E); }, 22.0f, 5.0f);
        // sidewalk trees (skip when they would block shop frontages: handled by reservation rects)
        auto treeOk = [&](float x, float z) {
          for (const RectF& r : noTree_) if (r.contains(x, z)) return false;
          return true;
        };
        if (bn) along(B.x0 + 3, B.x1 - 3, [&](float x) { if (treeOk(x, B.z0 + 1.6f)) tree(x, B.z0 + 1.6f, rng_.chance(0.25f) ? 1 : 0); }, 15.0f, 9.0f);
        if (bs) along(B.x0 + 3, B.x1 - 3, [&](float x) { if (treeOk(x, B.z1 - 1.6f)) tree(x, B.z1 - 1.6f, rng_.chance(0.25f) ? 1 : 0); }, 15.0f, 2.0f);
        if (bw) along(B.z0 + 3, B.z1 - 3, [&](float z) { if (treeOk(B.x0 + 1.6f, z)) tree(B.x0 + 1.6f, z, 0); }, 16.0f, 8.0f);
        if (be) along(B.z0 + 3, B.z1 - 3, [&](float z) { if (treeOk(B.x1 - 1.6f, z)) tree(B.x1 - 1.6f, z, 0); }, 16.0f, 3.0f);
      }
  }

  // --- special places ------------------------------------------------------------------------------------
  void gasStation(const RectF& B) {
    sidewalkBands(B, true, true, true, true);
    RectF L = lotOf(B, true, true, true, true);  // x 8..40, z -40..-9
    // forecourt asphalt + concrete under the canopy
    ground(L, 0.0f, mat::asphalt, 6.0f, {0.92f, 0.92f, 0.92f});
    w_.lowRects.push_back(L);
    w_.mapRoads.push_back(L);
    RectF under{10.5f, -29.0f, 33.5f, -10.0f};
    ground(under, 0.01f, mat::garage_floor, 5.0f);
    w_.walkable.push_back({8.0f, -12.0f, 40.0f, -9.0f});
    // store building along the north edge, facade facing south
    RectF store{12.0f, -40.0f, 36.0f, -32.0f};
    Facade f{mat::shop_posto, {0.92f, 0.92f, 0.9f}, 4.6f, true, 4.6f};
    houseBuilding(store, S, f);
    w_.poiGasDoor = {24.0f, 0, -31.2f};
    // canopy
    float cy = 5.2f;
    {
      MeshBuilder b = mb(22, -20, {0.9f, 0.9f, 0.9f});
      b.box(AABB({10.0f, cy, -28.5f}, {34.0f, cy + 0.5f, -10.5f}), mat::white, mat::roof_metal, 4.0f);
      MeshBuilder s1 = mb(22, -20, {0.07f, 0.22f, 0.62f});
      s1.box(AABB({9.9f, cy + 0.02f, -28.6f}, {34.1f, cy + 0.5f, -28.5f + 0.0f}), mat::white, mat::white, 1.0f);
      MeshBuilder s2 = mb(22, -20, {0.98f, 0.78f, 0.1f});
      s2.box(AABB({9.9f, cy - 0.12f, -28.6f}, {34.1f, cy + 0.0f, -10.4f}), mat::white, mat::white, 1.0f);
    }
    // columns
    for (float cx : {13.5f, 30.5f})
      for (float cz : {-27.0f, -23.0f, -15.0f, -11.5f}) {
        if (cz == -27.0f || cz == -11.5f) continue;
        MeshBuilder c = mb(cx, cz, {0.88f, 0.88f, 0.9f});
        c.box(AABB({cx - 0.3f, 0, cz - 0.3f}, {cx + 0.3f, cy, cz + 0.3f}), mat::white, mat::white, 1.0f);
        collider(AABB({cx - 0.3f, 0, cz - 0.3f}, {cx + 0.3f, cy, cz + 0.3f}), ColKind::Pole);
      }
    // islands + pumps
    int pid = 0;
    for (float iz : {-23.0f, -15.0f}) {
      MeshBuilder b = mb(22, iz, {0.8f, 0.8f, 0.8f});
      b.box(AABB({15.0f, 0, iz - 0.9f}, {29.0f, 0.2f, iz + 0.9f}), mat::concrete, mat::sidewalk, 3.0f);
      collider(AABB({15.0f, 0, iz - 0.9f}, {29.0f, 0.3f, iz + 0.9f}), ColKind::Prop);
      for (float px : {19.0f, 25.0f}) {
        prop("bomba", px, iz, 0, false);
        collider(AABB({px - 0.4f, 0.2f, iz - 0.3f}, {px + 0.4f, 1.8f, iz + 0.3f}), ColKind::Prop);
        w_.pumps.push_back({pid++, {px, 0.2f, iz}, 0});
      }
    }
    // price totem at the corner
    {
      MeshBuilder p = mb(10, -10, {0.3f, 0.3f, 0.34f});
      p.prism({10.2f, kH, -9.8f}, 0.18f, 8.0f, 8, mat::metal);
      MeshBuilder s = mb(10, -10, {1, 1, 1});
      s.box(AABB({8.6f, 7.4f, -10.2f}, {11.8f, 9.4f, -9.6f}), mat::shop_posto, mat::white, 3.0f);
      collider(AABB({10.0f, 0, -10.0f}, {10.4f, 9, -9.6f}), ColKind::Pole);
    }
    // tyre stack, bins, ice freezer area
    prop("lixeira", 38.0f, -31.0f, 0, true, 0.4f, 1.0f);
    prop("lixeira", 11.0f, -31.0f, 0, true, 0.4f, 1.0f);
    prop("pneus", 37.0f, -33.5f, 0.5f, true, 0.6f, 1.0f);
    prop("cone", 14.0f, -12.2f, 0, false);
    prop("cone", 30.0f, -12.2f, 0, false);
    w_.poiGas = {22.0f, 0, -19.0f};
    w_.gasAttendant = {22.0f, 0.2f, -19.2f};
    noTree_.push_back(L.inflated(2.6f));
    // NPC
    w_.npcs.push_back({"frentista", 1, {21.5f, 0.2f, -18.6f}, 1.2f});
    // service cars waiting
    w_.mapBuildings.push_back(store);
  }

  void market(const RectF& B) {
    sidewalkBands(B, true, true, true, true);
    RectF L = lotOf(B, true, true, true, true);  // 8..40, 9..40
    // parking lot
    ground({L.x0, 21.0f, L.x1, L.z1}, 0.0f, mat::asphalt, 6.0f, {0.88f, 0.88f, 0.9f});
    ground({L.x0, 9.0f, L.x1, 21.0f}, 0.0f, mat::asphalt, 6.0f, {0.95f, 0.95f, 0.95f});
    w_.lowRects.push_back({L.x0, 9.0f, L.x1, L.z1});
    w_.mapRoads.push_back({L.x0, 9.0f, L.x1, L.z1});
    noTree_.push_back(L.inflated(2.6f));
    // building
    RectF bld{10.0f, 9.0f, 34.0f, 21.0f};
    Facade f{mat::shop_mercado, {0.95f, 0.85f, 0.55f}, 5.2f, true, 5.2f};
    houseBuilding(bld, N, f);
    w_.poiMarket = {22.0f, 0, 6.5f};
    w_.poiMarketDoor = {22.0f, 0, 8.2f};
    // front walkway apron in front of the entrance is the sidewalk itself
    // parking stripes: back row and middle row
    for (float x = 10.0f; x < 38.5f; x += 2.7f) {
      paint({x, 34.5f, x + 0.12f, 40.0f}, {0.95f, 0.95f, 0.92f});
      paint({x, 23.5f, x + 0.12f, 28.5f}, {0.95f, 0.95f, 0.92f});
    }
    paint({10.0f, 34.5f, 38.5f, 34.62f}, {0.95f, 0.95f, 0.92f});
    // parked cars
    parkedCar(1, 1, 11.4f, 31.0f, 0.0f);   // sedan white, back row facing north
    parkedCar(0, 1, 16.8f, 31.0f, 0.0f);
    parkedCar(2, 2, 25.0f, 31.0f, 0.0f);
    parkedCar(0, 2, 35.5f, 31.0f, 0.0f);
    parkedCar(1, 2, 28.0f, 26.0f, kPi);
    // bins, carts, trees in planters
    prop("lixeira", 9.5f, 22.0f, 0, true, 0.4f, 1.0f);
    prop("lixeira", 35.0f, 22.0f, 0, true, 0.4f, 1.0f);
    prop("caixas", 36.0f, 10.2f, 0.3f, true, 0.6f, 1.0f);
    prop("caixas", 9.0f, 10.2f, 0.6f, true, 0.6f, 1.0f);
    for (float x : {10.0f, 22.0f, 38.0f}) {
      MeshBuilder b = mb(x, 22.5f, {0.5f, 0.5f, 0.5f});
      b.box(AABB({x - 1.0f, 0, 22.0f}, {x + 1.0f, 0.25f, 23.0f}), mat::concrete, mat::grass, 2.0f);
      tree(x, 22.5f, 0);
    }
    // interior door
    DoorDef d;
    d.id = 0; d.pos = {22.0f, 0, 8.0f}; d.radius = 2.0f; d.targetDoor = 1; d.arrive = {300.0f, 0, 4.0f}; d.arriveYaw = 0; d.label = "Entrar no Mercado do Zé"; d.toInterior = true;
    w_.doors.push_back(d);
    w_.npcs.push_back({"vendedor_externo", 0, {36.0f, 0, 12.0f}, 3.0f});
    w_.npcs.pop_back();
  }

  void workshop(const RectF& B) {
    sidewalkBands(B, true, true, true, true);
    RectF L = lotOf(B, true, true, true, true);  // -40..-8, 9..40
    ground({L.x0, 9.0f, L.x1, 28.0f}, 0.0f, mat::garage_floor, 5.0f);
    ground({L.x0, 28.0f, L.x1, L.z1}, 0.0f, mat::asphalt, 6.0f);
    w_.lowRects.push_back({L.x0, 9.0f, L.x1, L.z1});
    w_.mapRoads.push_back({L.x0, 9.0f, L.x1, L.z1});
    noTree_.push_back(L.inflated(2.6f));
    RectF bld{-38.0f, 28.0f, -14.0f, 40.0f};
    Facade f{mat::shop_oficina, {0.62f, 0.64f, 0.68f}, 5.5f, true, 5.5f};
    houseBuilding(bld, N, f);
    // service bay: yellow painted rectangle in front of the doors
    RectF bay{-34.0f, 20.5f, -18.0f, 27.5f};
    w_.serviceBay = bay;
    paint({bay.x0, bay.z0, bay.x1, bay.z0 + 0.18f}, {0.98f, 0.8f, 0.1f});
    paint({bay.x0, bay.z1 - 0.18f, bay.x1, bay.z1}, {0.98f, 0.8f, 0.1f});
    paint({bay.x0, bay.z0, bay.x0 + 0.18f, bay.z1}, {0.98f, 0.8f, 0.1f});
    paint({bay.x1 - 0.18f, bay.z0, bay.x1, bay.z1}, {0.98f, 0.8f, 0.1f});
    for (float x = bay.x0 + 1.0f; x < bay.x1 - 1.0f; x += 2.0f)
      paint({x, bay.z0 + 0.4f, x + 0.9f, bay.z0 + 0.62f}, {0.98f, 0.8f, 0.1f});
    w_.poiWorkshop = {-26.0f, 0, 24.0f};
    w_.workshopMechanic = {-15.8f, 0, 25.5f};
    // props
    prop("pneus", -13.0f, 30.0f, 0.4f, true, 0.6f, 1.0f);
    prop("tambor", -12.2f, 33.0f, 0, true, 0.35f, 1.0f);
    prop("tambor", -12.2f, 34.2f, 0, true, 0.35f, 1.0f);
    prop("cone", -35.0f, 19.0f, 0, false);
    prop("cone", -17.0f, 19.0f, 0, false);
    prop("caixas", -39.0f, 36.0f, 0.2f, true, 0.6f, 1.0f);
    prop("lixeira", -10.0f, 12.0f, 0, true, 0.4f, 1.0f);
    parkedCar(1, 2, -36.0f, 13.5f, 1.57f);   // customer car waiting
    parkedCar(0, 2, -36.0f, 16.5f, 1.57f);
    w_.npcs.push_back({"mecanico", 2, {-15.8f, 0, 25.5f}, -1.57f});
  }

  void plaza(const RectF& B) {
    sidewalkBands(B, true, true, true, true);
    RectF L = lotOf(B, true, true, true, true);  // -40..-8, -40..-9
    ground(L, kH, mat::grass, 4.0f, {0.88f, 0.98f, 0.82f});
    w_.mapGreen.push_back(L);
    // houses along the north side
    plotRow(N, L.x0, L.x1, L.z0, 11.0f);
    RectF pl{L.x0 + 3.0f, L.z0 + 13.5f, L.x1 - 3.0f, L.z1 - 2.0f};
    // paths made of pedra portuguesa
    RectF pathH{pl.x0, pl.cz() - 1.8f, pl.x1, pl.cz() + 1.8f};
    RectF pathV{pl.cx() - 1.8f, pl.z0, pl.cx() + 1.8f, pl.cz() - 1.8f};
    RectF pathV2{pl.cx() - 1.8f, pl.cz() + 1.8f, pl.cx() + 1.8f, pl.z1};
    for (const RectF& r : {pathH, pathV, pathV2}) {
      ground(r, kH + 0.012f, mat::pedra_port, 3.0f);
      w_.walkable.push_back(r);
      w_.mapPlaza.push_back(r);
    }
    RectF centre{pl.cx() - 4.5f, pl.cz() - 4.5f, pl.cx() + 4.5f, pl.cz() + 4.5f};
    ground(centre, kH + 0.014f, mat::pedra_port, 3.0f, {1.0f, 1.0f, 1.0f});
    w_.walkable.push_back(centre);
    w_.mapPlaza.push_back(centre);
    w_.walkable.push_back({L.x0, L.z0 + 12.0f, L.x1, L.z1});  // lawn is walkable too
    // fountain / monument in the middle
    {
      MeshBuilder b = mb(pl.cx(), pl.cz(), {0.85f, 0.85f, 0.85f});
      b.box(AABB({pl.cx() - 1.5f, kH, pl.cz() - 1.5f}, {pl.cx() + 1.5f, kH + 0.55f, pl.cz() + 1.5f}), mat::concrete, mat::concrete, 2.0f);
      MeshBuilder b2 = mb(pl.cx(), pl.cz(), {0.2f, 0.55f, 0.75f});
      b2.groundRect(pl.cx() - 1.2f, pl.cz() - 1.2f, pl.cx() + 1.2f, pl.cz() + 1.2f, kH + 0.5f, mat::white, 4.0f);
      MeshBuilder b3 = mb(pl.cx(), pl.cz(), {0.8f, 0.8f, 0.78f});
      b3.prism({pl.cx(), kH + 0.5f, pl.cz()}, 0.28f, 1.5f, 10, mat::concrete);
      collider(AABB({pl.cx() - 1.5f, 0, pl.cz() - 1.5f}, {pl.cx() + 1.5f, 2.0f, pl.cz() + 1.5f}), ColKind::Prop);
    }
    // benches around the centre, bins, orelhao, trees
    for (int k = 0; k < 4; ++k) {
      float a = k * kPi / 2 + kPi / 4;
      prop("banco", pl.cx() + std::sin(a) * 6.0f, pl.cz() - std::cos(a) * 6.0f, a + kPi, true, 0.8f, 0.9f);
    }
    prop("lixeira", pl.x0 + 1.0f, pl.cz() - 3.0f, 0, true, 0.4f, 1.0f);
    prop("lixeira", pl.x1 - 1.0f, pl.cz() + 3.0f, 0, true, 0.4f, 1.0f);
    prop("orelhao", pl.x1 - 0.8f, pl.z1 - 0.8f, kPi, true, 0.4f, 2.0f);
    prop("hidrante", pl.x0 + 0.8f, pl.z1 - 0.8f, 0, true, 0.2f, 0.8f);
    for (int k = 0; k < 11; ++k) {
      float x = rng_.range(pl.x0 + 1.5f, pl.x1 - 1.5f), z = rng_.range(pl.z0 + 1.5f, pl.z1 - 1.5f);
      if (pathH.inflated(1.2f).contains(x, z) || pathV.inflated(1.2f).contains(x, z) || pathV2.inflated(1.2f).contains(x, z) || centre.inflated(1.5f).contains(x, z)) continue;
      tree(x, z, k % 4 == 0 ? 1 : (k % 4 == 1 ? 2 : 0));
    }
    // kiosk
    RectF kiosk{pl.x0 + 1.0f, pl.z0 + 1.0f, pl.x0 + 5.0f, pl.z0 + 4.0f};
    Facade kf{mat::house_yellow, {0.95f, 0.7f, 0.3f}, 3.0f, false, 3.0f};
    houseBuilding(kiosk, S, kf);
    w_.npcs.push_back({"vizinho", 3, {pl.cx() + 6.5f, kH, pl.cz() + 0.5f}, 1.6f});
    w_.neighbour = {pl.cx() + 6.5f, kH, pl.cz() + 0.5f};
  }

  void boundary() {
    float H = World::kHalf, t = 1.0f;
    // collider walls on the world edge
    collider(AABB({-H - t, 0, -H - t}, {H + t, 6, -H}), ColKind::Wall);
    collider(AABB({-H - t, 0, H}, {H + t, 6, H + t}), ColKind::Wall);
    collider(AABB({-H - t, 0, -H}, {-H, 6, H}), ColKind::Wall);
    collider(AABB({H, 0, -H}, {H + t, 6, H}), ColKind::Wall);
    // backdrop: ring of tall buildings outside the playable area
    Rng r(777);
    const int layers[4] = {mat::apt_beige, mat::apt_green, mat::apt_bands, mat::sobrado_pink};
    for (int side = 0; side < 4; ++side) {
      float p = -H;
      while (p < H) {
        float wd = r.range(14.0f, 22.0f), dep = r.range(12.0f, 18.0f), h = r.range(13.0f, 24.0f);
        RectF b;
        int front;
        switch (side) {
          case 0: b = {p, -H - dep - 0.5f, p + wd, -H - 0.5f}; front = S; break;
          case 1: b = {p, H + 0.5f, p + wd, H + dep + 0.5f}; front = N; break;
          case 2: b = {-H - dep - 0.5f, p, -H - 0.5f, p + wd}; front = E; break;
          default: b = {H + 0.5f, p, H + dep + 0.5f, p + wd}; front = W; break;
        }
        wallsFor(b, h, front, layers[r.irange(0, 3)], {0.8f, 0.78f, 0.72f});
        MeshBuilder rb = mb(b.cx(), b.cz(), {0.75f, 0.75f, 0.75f});
        rb.roofRect(b.x0, b.z0, b.x1, b.z1, h, mat::roof_laje, 3.0f);
        p += wd;
      }
    }
    // ground outside the wall so the horizon is closed
    ground({-H - 20, -H - 20, H + 20, -H}, kH, mat::grass, 6.0f);
    ground({-H - 20, H, H + 20, H + 20}, kH, mat::grass, 6.0f);
    ground({-H - 20, -H, -H, H}, kH, mat::grass, 6.0f);
    ground({H, -H, H + 20, H}, kH, mat::grass, 6.0f);
  }

  // --- the market interior --------------------------------------------------------------------------------------
  void marketInterior() {
    const float X0 = 292, X1 = 308, Z0 = -6, Z1 = 6, HGT = 3.2f;
    w_.market.bounds = {X0, Z0, X1, Z1};
    w_.market.height = HGT;
    w_.market.spawn = {300.0f, 0, 3.6f};
    w_.market.spawnYaw = 0;
    MeshBuilder b = mb(300, 0, {1, 1, 1});
    b.groundRect(X0, Z0, X1, Z1, 0, mat::tile_floor, 2.0f);
    // inward facing walls
    b.setTint({0.95f, 0.92f, 0.84f});
    b.wall(X1, Z1, X0, Z1, 0, HGT, mat::wall_paint, 0, (X1 - X0) / 4.0f, 0, HGT / 4.0f, 0.82f, 1.0f);  // south wall seen from inside
    b.wall(X0, Z0, X1, Z0, 0, HGT, mat::wall_paint, 0, (X1 - X0) / 4.0f, 0, HGT / 4.0f, 0.82f, 1.0f);  // north
    b.wall(X0, Z1, X0, Z0, 0, HGT, mat::wall_paint, 0, (Z1 - Z0) / 4.0f, 0, HGT / 4.0f, 0.82f, 1.0f);  // west
    b.wall(X1, Z0, X1, Z1, 0, HGT, mat::wall_paint, 0, (Z1 - Z0) / 4.0f, 0, HGT / 4.0f, 0.82f, 1.0f);  // east
    // red stripe band for identity
    b.setTint({0.78f, 0.12f, 0.1f});
    b.wall(X1, Z1 - 0.01f, X0, Z1 - 0.01f, 1.9f, 2.25f, mat::white, 0, 1, 0, 1, 1, 1);
    b.wall(X0, Z0 + 0.01f, X1, Z0 + 0.01f, 1.9f, 2.25f, mat::white, 0, 1, 0, 1, 1, 1);
    b.wall(X0 + 0.01f, Z1, X0 + 0.01f, Z0, 1.9f, 2.25f, mat::white, 0, 1, 0, 1, 1, 1);
    b.wall(X1 - 0.01f, Z0, X1 - 0.01f, Z1, 1.9f, 2.25f, mat::white, 0, 1, 0, 1, 1, 1);
    // wall colliders (thick, outside the room)
    collider(AABB({X0 - 1, 0, Z0 - 1}, {X1 + 1, HGT, Z0}), ColKind::Wall);
    collider(AABB({X0 - 1, 0, Z1}, {X1 + 1, HGT, Z1 + 1}), ColKind::Wall);
    collider(AABB({X0 - 1, 0, Z0}, {X0, HGT, Z1}), ColKind::Wall);
    collider(AABB({X1, 0, Z0}, {X1 + 1, HGT, Z1}), ColKind::Wall);
    auto interiorBox = [&](float x0, float z0, float x1, float z1, float y1, int side, int top, Vec3 tint) {
      MeshBuilder bb = mb(300, 0, tint);
      bb.box(AABB({x0, 0, z0}, {x1, y1, z1}), side, top, 2.0f);
      collider(AABB({x0, 0, z0}, {x1, y1, z1}), ColKind::Prop);
      w_.interiorBlockers.push_back({x0, z0, x1, z1});
      w_.mapBuildings.size();
    };
    // gondolas with product shelves
    for (float gz : {-2.4f, 1.6f}) interiorBox(294.0f, gz - 0.35f, 304.0f, gz + 0.35f, 1.75f, mat::shelf, mat::white, {1, 1, 1});
    // fridges along the west wall
    interiorBox(X0, -5.0f, X0 + 0.9f, 3.0f, 2.0f, mat::metal, mat::white, {0.85f, 0.92f, 1.0f});
    // checkout counter
    interiorBox(303.6f, -4.0f, 306.8f, -2.9f, 1.0f, mat::wood, mat::wood, {0.9f, 0.8f, 0.7f});
    // door in the south wall (visual)
    MeshBuilder d = mb(300, 0, {0.5f, 0.55f, 0.6f});
    d.wall(301.0f, Z1 - 0.02f, 299.0f, Z1 - 0.02f, 0, 2.4f, mat::metal, 0, 1, 0, 1, 1, 1);
    // interior navigation
    w_.interiorWalkable.push_back({X0 + 0.5f, Z0 + 0.5f, X1 - 0.5f, Z1 - 0.6f});
    // products on the gondola faces
    int pid = 0;
    const int itemsA[3] = {1, 2, 3}, itemsB[3] = {6, 7, 8};
    for (int i = 0; i < 3; ++i) {
      w_.products.push_back({pid++, itemsA[i], {295.5f + i * 2.5f, 0, -1.7f}});
      w_.products.push_back({pid++, itemsB[i], {295.5f + i * 2.5f, 0, 0.9f}});
    }
    w_.marketClerk = {305.2f, 0, -4.8f};
    w_.npcs.push_back({"atendente", 4, {305.2f, 0, -4.8f}, kPi, true});
    DoorDef ex;
    ex.id = 1; ex.pos = {300.0f, 0, 5.2f}; ex.radius = 1.8f; ex.targetDoor = 0; ex.arrive = {22.0f, kH, 6.9f}; ex.arriveYaw = kPi; ex.label = "Sair do mercado"; ex.toInterior = false;
    w_.doors.push_back(ex);
    // ceiling (only drawn when the camera is below it)
    MeshBuilder cb(&w_.marketCeiling);
    cb.setTint({0.92f, 0.92f, 0.9f});
    cb.ceiling(X0, Z0, X1, Z1, HGT, mat::wall_paint, 4.0f);
    cb.setTint({1.0f, 1.0f, 0.95f});
    for (float lx : {296.0f, 300.0f, 304.0f})
      for (float lz : {-3.0f, 0.0f, 3.0f}) cb.ceiling(lx - 0.6f, lz - 0.2f, lx + 0.6f, lz + 0.2f, HGT - 0.01f, mat::white, 1.0f);
  }

  void run() {
    // blocks
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j) {
        RectF B{kBx[i].a, kBz[j].a, kBx[i].b, kBz[j].b};
        bool n = j > 0, s = j < 3, wst = i > 0, e = i < 3;
        bool inner = (i == 1 || i == 2) && (j == 1 || j == 2);
        if (i == 2 && j == 1) { gasStation(B); continue; }
        if (i == 2 && j == 2) { market(B); continue; }
        if (i == 1 && j == 2) { workshop(B); continue; }
        if (i == 1 && j == 1) { plaza(B); continue; }
        blockGeneric(B, n, e, s, wst, inner);
      }
    streets();
    streetsParked();
    furniture();
    boundary();
    marketInterior();

    // spawn + vehicles
    w_.spawnPlayer = {-21.0f, kH, -8.3f};
    w_.spawnYaw = kPi;  // facing south (towards the street)
    w_.vehicleSpawn[0] = {-17.0f, 0, -4.9f}; w_.vehicleYaw[0] = -kPi / 2;     // compacto at the north curb of the main street
    w_.vehicleSpawn[1] = {26.0f, 0, 26.0f}; w_.vehicleYaw[1] = 0;            // sedan in the market parking lot
    w_.vehicleSpawn[2] = {-24.0f, 0, 14.0f}; w_.vehicleYaw[2] = 0.0f;        // picape in the workshop forecourt
    // wandering pedestrians on the pavement
    struct P { const char* a; float x, z; };
    const P walkers[] = {{"mulher_rosa", -30, -10.5f}, {"homem_polo", -12, 8.5f}, {"jovem_moletom", 10, -8.5f}, {"mulher_vestido", 30, 8.2f},
                         {"corredor", -45, -20}, {"homem_polo", 45, 30}, {"mulher_rosa", 3, -30}, {"jovem_moletom", -50, 40},
                         {"mulher_vestido", 52, -10}, {"corredor", 20, 45}, {"vizinho", -40, 12}, {"mulher_rosa", 0, 20}};
    for (const P& p : walkers) w_.npcs.push_back({p.a, 0, {p.x, kH, p.z}, rng_.range(0, kTau)});
  }

  void streetsParked() {
    // parallel parking on both curbs; skip spots in front of intersections / crossings / special blocks
    struct Spot { int model, color; float x, z, yaw; };
    const float cz1 = kZs[1] - kHwZ[1] + 1.4f;   // main street north curb lane centre
    const float cz2 = kZs[1] + kHwZ[1] - 1.4f;   // main street south curb
    const Spot spots[] = {
        {1, 1, -38.0f, cz1, -kPi / 2}, {0, 1, -9.0f, cz2, kPi / 2}, {2, 1, 14.0f, cz1, -kPi / 2}, {0, 2, 32.0f, cz2, kPi / 2},
        {1, 2, -62.0f, cz2, kPi / 2}, {2, 2, 60.0f, cz1, -kPi / 2}, {0, 1, -28.0f, cz2, kPi / 2},
        {1, 1, -kHwX[1] + 1.4f - 0.0f + 0.0f - 0.0f, -30.0f, kPi}, {2, 1, kHwX[1] - 1.4f, 24.0f, 0.0f}, {0, 2, -(kHwX[1] - 1.4f), 30.0f, kPi},
        {1, 1, kXs[0] - kHwX[0] + 1.4f, -24.0f, kPi}, {0, 1, kXs[2] + kHwX[2] - 1.4f, 20.0f, 0.0f}, {2, 2, kXs[2] - kHwX[2] + 1.4f, -20.0f, kPi},
        {0, 1, 0.0f - 0.0f, 0.0f, 0.0f}};
    for (const Spot& s : spots) {
      if (&s == &spots[13]) break;
      // keep clear of the player's start and vehicle spawns
      if (std::fabs(s.x - (-17.0f)) < 5.0f && std::fabs(s.z - (-4.9f)) < 3.0f) continue;
      parkedCar(s.model, s.color, s.x, s.z, s.yaw);
    }
  }

  std::vector<RectF> noTree_;

 private:
  World& w_;
  Rng rng_;
  std::map<std::pair<int, int>, int> map_;
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
  int cx0 = std::max(0, (int)std::floor(x0 / gridCell) - gridMinX), cx1 = std::min(gridW - 1, (int)std::floor(x1 / gridCell) - gridMinX);
  int cz0 = std::max(0, (int)std::floor(z0 / gridCell) - gridMinZ), cz1 = std::min(gridH - 1, (int)std::floor(z1 / gridCell) - gridMinZ);
  for (int z = cz0; z <= cz1; ++z)
    for (int x = cx0; x <= cx1; ++x)
      for (int id : grid[(size_t)z * gridW + x]) out.push_back(id);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

float World::heightAt(float x, float z) const {
  if (market.bounds.contains(x, z)) return 0.0f;
  for (const RectF& r : lowRects)
    if (r.contains(x, z)) return 0.0f;
  return kSidewalkH;
}

void buildWorld(World& w) {
  Gen g(w);
  g.run();
  for (auto& c : w.chunks) {
    c.bounds = c.mesh.bounds;
  }
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
  // base: dark blue-grey ground
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) put(x, y, 0.12f, 0.14f, 0.17f);
  auto fill = [&](const RectF& r, float cr, float cg, float cb) {
    int x0 = (int)std::floor((r.x0 + extent) * k), x1 = (int)std::ceil((r.x1 + extent) * k);
    int y0 = (int)std::floor((r.z0 + extent) * k), y1 = (int)std::ceil((r.z1 + extent) * k);
    for (int y = std::max(0, y0); y < std::min(size, y1); ++y)
      for (int x = std::max(0, x0); x < std::min(size, x1); ++x) put(x, y, cr, cg, cb);
  };
  for (const RectF& r : w.mapGreen) fill(r, 0.17f, 0.28f, 0.2f);
  for (const RectF& r : w.mapWalk) fill(r, 0.34f, 0.37f, 0.4f);
  for (const RectF& r : w.mapRoads) fill(r, 0.22f, 0.24f, 0.28f);
  for (const RectF& r : w.mapPlaza) fill(r, 0.46f, 0.46f, 0.42f);
  for (const RectF& r : w.mapBuildings) fill(r, 0.52f, 0.45f, 0.4f);
  // darker 1-texel frame so the clamp border is not bright
  for (int i = 0; i < size; ++i) { put(i, 0, 0.05f, 0.06f, 0.08f); put(i, size - 1, 0.05f, 0.06f, 0.08f); put(0, i, 0.05f, 0.06f, 0.08f); put(size - 1, i, 0.05f, 0.06f, 0.08f); }
}

}  // namespace gtabr
