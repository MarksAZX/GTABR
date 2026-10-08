#include "props3d.h"

#include <algorithm>
#include <cmath>

#include "material_ids.h"

namespace gtabr {

namespace {

Vec3 jitter(Rng& r, const Vec3& base, float amt) {
  return {clamp(base.x * (1.0f + r.range(-amt, amt)), 0.0f, 1.6f), clamp(base.y * (1.0f + r.range(-amt, amt)), 0.0f, 1.6f),
          clamp(base.z * (1.0f + r.range(-amt, amt)), 0.0f, 1.6f)};
}

// Axis aligned box in local coordinates (world-space UV tiling keeps texture scale constant).
void lbox(MeshBuilder& b, float x0, float y0, float z0, float x1, float y1, float z1, int layer, float tile = 1.5f) {
  b.box(AABB({x0, y0, z0}, {x1, y1, z1}), layer, layer, tile);
}

struct Crown {
  Vec3 c, r;
  Vec3 tint;
};

}  // namespace

float buildTree3D(MeshBuilder& b, MeshBuilder* lod, Rng& rng, int species, const Vec3& pos, float yaw, float scale) {
  b.setXform(pos, yaw, scale);
  b.setEmissive(0);
  const uint32_t seed = rng.next();
  float height = 6.0f;
  auto trunk = [&](Vec3 base, Vec3 top, float r0, float r1, Vec3 tint, int sides = 7) {
    b.setTint(tint);
    b.frustum(base, top, r0, r1, sides, mat::bark, true, false, 0.7f);
  };
  auto crown = [&](const Crown& c, int segs = 14, int rings = 7, float lump = 0.2f) {
    b.setTint(c.tint);
    b.blob(c.c, c.r, segs, rings, lump, rng.next(), mat::foliage, 0.5f);
  };
  std::vector<Crown> cs;
  Vec3 leaf;
  switch (species) {
    case 0: {   // mangueira: short thick trunk, huge dense dark dome
      leaf = jitter(rng, {0.46f, 0.72f, 0.34f}, 0.12f);
      float lx = rng.range(-0.25f, 0.25f), lz = rng.range(-0.25f, 0.25f);
      trunk({0, 0, 0}, {lx, 2.5f, lz}, 0.3f, 0.2f, {0.55f, 0.5f, 0.45f});
      trunk({lx, 2.2f, lz}, {lx + 1.0f, 3.5f, lz + 0.3f}, 0.14f, 0.08f, {0.55f, 0.5f, 0.45f});
      trunk({lx, 2.3f, lz}, {lx - 0.9f, 3.4f, lz - 0.5f}, 0.13f, 0.08f, {0.55f, 0.5f, 0.45f});
      cs.push_back({{lx, 4.6f, lz}, {2.5f, 1.8f, 2.5f}, leaf});
      int n = rng.irange(4, 6);
      for (int i = 0; i < n; ++i) {
        float a = i * kTau / n + rng.range(-0.3f, 0.3f), d = rng.range(1.3f, 1.9f);
        cs.push_back({{lx + std::cos(a) * d, rng.range(3.5f, 4.9f), lz + std::sin(a) * d}, {rng.range(1.4f, 1.9f), rng.range(1.1f, 1.5f), rng.range(1.4f, 1.9f)},
                      jitter(rng, leaf, 0.1f)});
      }
      height = 6.6f;
      break;
    }
    case 1: case 2: {   // ipe: slender trunk, forked, open crown covered in yellow / lilac flowers
      Vec3 flower = species == 1 ? Vec3{1.0f, 0.84f, 0.16f} : Vec3{0.9f, 0.46f, 0.8f};
      leaf = jitter(rng, {0.5f, 0.75f, 0.36f}, 0.1f);
      float lx = rng.range(-0.3f, 0.3f);
      trunk({0, 0, 0}, {lx, 3.2f, 0}, 0.18f, 0.11f, {0.6f, 0.55f, 0.5f});
      trunk({lx, 2.8f, 0}, {lx + 1.2f, 4.5f, 0.4f}, 0.1f, 0.06f, {0.6f, 0.55f, 0.5f});
      trunk({lx, 2.9f, 0}, {lx - 1.0f, 4.3f, -0.5f}, 0.1f, 0.06f, {0.6f, 0.55f, 0.5f});
      float flowering = rng.chance(0.7f) ? 1.0f : 0.0f;
      int n = rng.irange(5, 7);
      for (int i = 0; i < n; ++i) {
        float a = i * kTau / n + rng.range(-0.4f, 0.4f), d = rng.range(0.4f, 1.6f);
        Vec3 t = (rng.chance(0.75f) && flowering > 0.5f) ? jitter(rng, flower, 0.07f) : leaf;
        cs.push_back({{lx + std::cos(a) * d, rng.range(4.0f, 5.5f), std::sin(a) * d}, {rng.range(1.0f, 1.5f), rng.range(0.8f, 1.1f), rng.range(1.0f, 1.5f)}, t});
      }
      height = 6.0f;
      break;
    }
    case 3: case 4: {   // palms
      bool coco = species == 3;
      float h = coco ? rng.range(6.5f, 8.5f) : rng.range(8.0f, 10.5f);
      float bend = coco ? rng.range(0.6f, 1.6f) : 0.1f;
      float ba = rng.range(0, kTau);
      Vec3 prev{0, 0, 0};
      const int segs = 7;
      for (int i = 1; i <= segs; ++i) {
        float t = (float)i / segs;
        Vec3 p{std::cos(ba) * bend * t * t, h * t, std::sin(ba) * bend * t * t};
        float r0 = coco ? lerp(0.24f, 0.15f, (float)(i - 1) / segs) : lerp(0.27f, 0.2f, (float)(i - 1) / segs);
        float r1 = coco ? lerp(0.24f, 0.15f, t) : lerp(0.27f, 0.2f, t);
        Vec3 tint = coco ? (i % 2 ? Vec3{0.62f, 0.55f, 0.48f} : Vec3{0.5f, 0.44f, 0.38f}) : Vec3{0.78f, 0.76f, 0.72f};
        trunk(prev, p, r0, r1, tint, 8);
        prev = p;
      }
      Vec3 top = prev;
      int nf = coco ? rng.irange(9, 12) : rng.irange(8, 10);
      b.setTint(jitter(rng, coco ? Vec3{0.45f, 0.72f, 0.3f} : Vec3{0.38f, 0.66f, 0.28f}, 0.1f));
      for (int i = 0; i < nf; ++i) {
        float a = i * kTau / nf + rng.range(-0.25f, 0.25f);
        float up = coco ? rng.range(0.15f, 0.65f) : rng.range(0.1f, 0.5f);
        Vec3 d{std::cos(a), up, std::sin(a)};
        b.frond(top, d, coco ? rng.range(2.8f, 3.6f) : rng.range(2.4f, 3.0f), coco ? 0.5f : 0.42f, coco ? rng.range(1.2f, 1.8f) : rng.range(1.0f, 1.5f), 5, mat::foliage);
      }
      // a spear leaf standing straight up + coconuts / crownshaft
      b.frond(top, {0.05f, 1.0f, 0.0f}, 1.5f, 0.2f, 0.2f, 3, mat::foliage);
      if (coco) {
        b.setTint({0.4f, 0.3f, 0.12f});
        for (int i = 0; i < 3; ++i) {
          float a = i * kTau / 3 + 0.4f;
          b.blob(top + Vec3{std::cos(a) * 0.25f, -0.25f, std::sin(a) * 0.25f}, {0.14f, 0.16f, 0.14f}, 6, 3, 0.05f, 1, mat::bark, 0.8f);
        }
      } else {
        b.setTint({0.45f, 0.62f, 0.3f});
        b.frustum(top + Vec3{0, -0.9f, 0}, top + Vec3{0, 0.1f, 0}, 0.2f, 0.17f, 8, mat::foliage, true, false, 0.5f);
      }
      height = h + 1.5f;
      break;
    }
    case 5: {   // arbusto
      leaf = jitter(rng, {0.4f, 0.68f, 0.3f}, 0.18f);
      int n = rng.irange(3, 5);
      bool flowers = rng.chance(0.3f);
      Vec3 fcol = rng.chance(0.5f) ? Vec3{0.95f, 0.35f, 0.5f} : Vec3{0.98f, 0.95f, 0.85f};
      for (int i = 0; i < n; ++i) {
        float a = i * kTau / n + rng.range(-0.3f, 0.3f), d = i == 0 ? 0.0f : rng.range(0.3f, 0.55f);
        float rr = rng.range(0.45f, 0.75f);
        cs.push_back({{std::cos(a) * d, rr * 0.8f, std::sin(a) * d}, {rr, rr * 0.8f, rr}, (flowers && i % 2) ? jitter(rng, fcol, 0.06f) : jitter(rng, leaf, 0.08f)});
      }
      height = 1.2f;
      break;
    }
    case 6: {   // amendoeira: tiered flat crowns
      leaf = jitter(rng, {0.5f, 0.74f, 0.28f}, 0.1f);
      if (rng.chance(0.18f)) leaf = {0.85f, 0.4f, 0.2f};   // turning leaves
      trunk({0, 0, 0}, {0, 3.4f, 0}, 0.2f, 0.12f, {0.58f, 0.52f, 0.46f});
      for (int i = 0; i < 3; ++i) {
        float s = 1.0f - i * 0.28f;
        cs.push_back({{rng.range(-0.2f, 0.2f), 3.0f + i * 1.0f, rng.range(-0.2f, 0.2f)}, {2.3f * s, 0.55f, 2.3f * s}, jitter(rng, leaf, 0.08f)});
      }
      height = 6.0f;
      break;
    }
    default: {   // flamboyant: broad umbrella of fine leaves with red-orange flowers
      Vec3 flower{0.95f, 0.3f, 0.14f};
      leaf = jitter(rng, {0.46f, 0.7f, 0.3f}, 0.1f);
      bool bloom = rng.chance(0.65f);
      trunk({0, 0, 0}, {0.2f, 3.4f, 0}, 0.24f, 0.14f, {0.5f, 0.45f, 0.42f});
      trunk({0.2f, 3.0f, 0}, {1.6f, 4.3f, 0.5f}, 0.1f, 0.06f, {0.5f, 0.45f, 0.42f});
      trunk({0.2f, 3.0f, 0}, {-1.4f, 4.2f, -0.6f}, 0.1f, 0.06f, {0.5f, 0.45f, 0.42f});
      int n = rng.irange(5, 7);
      for (int i = 0; i < n; ++i) {
        float a = i * kTau / n + rng.range(-0.3f, 0.3f), d = rng.range(0.8f, 2.0f);
        cs.push_back({{0.2f + std::cos(a) * d, rng.range(4.3f, 5.0f), std::sin(a) * d}, {rng.range(1.2f, 1.7f), rng.range(0.6f, 0.85f), rng.range(1.2f, 1.7f)},
                      (bloom && rng.chance(0.7f)) ? jitter(rng, flower, 0.08f) : leaf});
      }
      height = 5.8f;
      break;
    }
  }
  for (const Crown& c : cs) { if (species == 5) crown(c, 8, 4, 0.2f); else crown(c); }
  b.clearXform();

  if (lod) {
    // far silhouette: one blob over a stick
    lod->setXform(pos, yaw, scale);
    lod->setEmissive(0);
    if (!cs.empty()) {
      Vec3 mn = cs[0].c - cs[0].r, mx = cs[0].c + cs[0].r;
      for (const Crown& c : cs) { mn = {std::min(mn.x, c.c.x - c.r.x), std::min(mn.y, c.c.y - c.r.y), std::min(mn.z, c.c.z - c.r.z)};
                                    mx = {std::max(mx.x, c.c.x + c.r.x), std::max(mx.y, c.c.y + c.r.y), std::max(mx.z, c.c.z + c.r.z)}; }
      lod->setTint(cs[0].tint);
      lod->blob((mn + mx) * 0.5f, (mx - mn) * 0.5f, 6, 3, 0.1f, 7, mat::foliage, 0.6f);
    } else {
      lod->setTint({0.4f, 0.65f, 0.3f});
      lod->frond({0, height, 0}, {1, 0.3f, 0}, 3.0f, 0.5f, 1.2f, 2, mat::foliage);
      lod->frond({0, height, 0}, {-1, 0.3f, 0}, 3.0f, 0.5f, 1.2f, 2, mat::foliage);
      lod->frond({0, height, 0}, {0, 0.3f, 1}, 3.0f, 0.5f, 1.2f, 2, mat::foliage);
      lod->frond({0, height, 0}, {0, 0.3f, -1}, 3.0f, 0.5f, 1.2f, 2, mat::foliage);
    }
    lod->setTint({0.55f, 0.5f, 0.45f});
    lod->frustum({0, 0, 0}, {0, std::min(height * 0.6f, 4.0f), 0}, 0.2f, 0.12f, 4, mat::bark);
    lod->clearXform();
  }
  return height;
}

void buildProp3D(MeshBuilder& b, Rng& rng, int model, const Vec3& pos, float yaw, float scale) {
  b.setXform(pos, yaw, scale);
  b.setEmissive(0);
  b.setTint({1, 1, 1});
  const Vec3 metalGrey{0.45f, 0.46f, 0.5f};
  switch (model) {
    case 0: {   // lixeira (cesto com tampa)
      b.setTint(jitter(rng, {0.2f, 0.45f, 0.32f}, 0.15f));
      b.frustum({0, 0, 0}, {0, 0.85f, 0}, 0.22f, 0.27f, 12, mat::metal, true, true);
      b.setTint({0.15f, 0.15f, 0.17f});
      b.frustum({0, 0.85f, 0}, {0, 0.95f, 0}, 0.3f, 0.28f, 12, mat::metal, true, false);
      b.frustum({0, 0.4f, 0}, {0, 0.44f, 0}, 0.275f, 0.275f, 12, mat::metal, false, false);
      break;
    }
    case 1: {   // banco de praca: ripas de madeira sobre pes de concreto
      b.setTint({0.78f, 0.78f, 0.76f});
      for (float x : {-0.68f, 0.68f}) lbox(b, x - 0.07f, 0, -0.24f, x + 0.07f, 0.42f, 0.24f, mat::concrete);
      b.setTint(jitter(rng, {0.62f, 0.42f, 0.25f}, 0.1f));
      for (int i = 0; i < 3; ++i) lbox(b, -0.82f, 0.42f, -0.22f + i * 0.15f, 0.82f, 0.46f, -0.1f + i * 0.15f, mat::wood);
      for (int i = 0; i < 2; ++i) lbox(b, -0.82f, 0.62f + i * 0.14f, -0.27f, 0.82f, 0.73f + i * 0.14f, -0.23f, mat::wood);
      b.setTint({0.3f, 0.3f, 0.33f});
      for (float x : {-0.68f, 0.68f}) lbox(b, x - 0.03f, 0.42f, -0.28f, x + 0.03f, 0.9f, -0.24f, mat::metal);
      break;
    }
    case 2: {   // poste de concreto com braco curvo e luminaria
      b.setTint({0.7f, 0.7f, 0.68f});
      b.frustum({0, 0, 0}, {0, 7.2f, 0}, 0.17f, 0.1f, 8, mat::concrete, true, false, 0.4f);
      b.setTint({0.4f, 0.4f, 0.43f});
      b.frustum({0, 6.8f, 0}, {0, 7.4f, 0.8f}, 0.055f, 0.045f, 6, mat::metal, false, false);
      b.frustum({0, 7.4f, 0.8f}, {0, 7.28f, 1.7f}, 0.045f, 0.04f, 6, mat::metal, false, false);
      b.setTint({0.3f, 0.3f, 0.33f});
      lbox(b, -0.17f, 7.2f, 1.45f, 0.17f, 7.32f, 2.0f, mat::metal);
      b.setTint({1.0f, 0.88f, 0.62f});
      b.setEmissive(0.95f);
      lbox(b, -0.13f, 7.17f, 1.5f, 0.13f, 7.2f, 1.95f, mat::white);
      b.setEmissive(0);
      // utility arm with insulators on the pole (service wires hang from here)
      b.setTint({0.4f, 0.4f, 0.43f});
      lbox(b, -0.8f, 6.35f, -0.04f, 0.8f, 6.42f, 0.04f, mat::metal);
      b.setTint({0.8f, 0.8f, 0.82f});
      for (float x : {-0.7f, 0.0f, 0.7f}) b.frustum({x, 6.42f, 0}, {x, 6.6f, 0}, 0.045f, 0.03f, 6, mat::white, true, false);
      if (rng.chance(0.3f)) {   // transformer can
        b.setTint({0.5f, 0.52f, 0.55f});
        b.frustum({0.4f, 5.5f, 0.0f}, {0.4f, 6.25f, 0.0f}, 0.22f, 0.22f, 10, mat::metal, true, true);
        lbox(b, 0.17f, 5.9f, -0.05f, 0.3f, 6.0f, 0.05f, mat::metal);
      }
      break;
    }
    case 3: {   // bomba de combustivel
      b.setTint({0.9f, 0.9f, 0.92f});
      lbox(b, -0.4f, 0.0f, -0.25f, 0.4f, 1.75f, 0.25f, mat::white);
      b.setTint({0.75f, 0.1f, 0.08f});
      lbox(b, -0.405f, 0.0f, -0.255f, 0.405f, 0.45f, 0.255f, mat::white);
      lbox(b, -0.405f, 1.6f, -0.255f, 0.405f, 1.75f, 0.255f, mat::white);
      b.setTint({0.1f, 0.1f, 0.12f});
      lbox(b, -0.3f, 1.2f, 0.25f, 0.3f, 1.5f, 0.27f, mat::metal);
      lbox(b, -0.3f, 1.2f, -0.27f, 0.3f, 1.5f, -0.25f, mat::metal);
      b.setTint({0.4f, 1.0f, 0.55f});
      b.setEmissive(0.8f);
      lbox(b, -0.22f, 1.26f, 0.27f, 0.22f, 1.42f, 0.275f, mat::white);
      lbox(b, -0.22f, 1.26f, -0.275f, 0.22f, 1.42f, -0.27f, mat::white);
      b.setEmissive(0);
      b.setTint({0.08f, 0.08f, 0.09f});
      for (float s : {-1.0f, 1.0f}) {
        b.frustum({0.4f, 1.1f, 0.0f}, {0.52f, 0.7f, 0.1f * s}, 0.03f, 0.03f, 6, mat::metal, false, false);
        b.frustum({0.52f, 0.7f, 0.1f * s}, {0.5f, 0.35f, 0.18f * s}, 0.03f, 0.03f, 6, mat::metal, false, false);
        if (s > 0) break;
      }
      break;
    }
    case 4: {   // orelhao
      b.setTint({0.95f, 0.5f, 0.12f});
      b.blob({0, 1.55f, 0}, {0.5f, 0.45f, 0.4f}, 12, 5, 0.0f, 3, mat::metal, 0.8f);
      b.frustum({0, 0, 0}, {0, 1.4f, 0}, 0.1f, 0.1f, 8, mat::metal, true, true);
      b.setTint({0.1f, 0.1f, 0.12f});
      b.blob({0, 1.55f, 0.28f}, {0.28f, 0.22f, 0.15f}, 8, 4, 0.0f, 3, mat::metal, 0.8f);
      break;
    }
    case 5: {   // hidrante
      b.setTint({0.8f, 0.12f, 0.08f});
      b.frustum({0, 0, 0}, {0, 0.65f, 0}, 0.13f, 0.11f, 10, mat::metal, false, true);
      b.blob({0, 0.7f, 0}, {0.15f, 0.12f, 0.15f}, 10, 4, 0.0f, 5, mat::metal, 0.8f);
      b.frustum({-0.22f, 0.45f, 0}, {0.22f, 0.45f, 0}, 0.06f, 0.06f, 8, mat::metal, true, true);
      b.frustum({0, 0.4f, 0}, {0, 0.4f, 0.2f}, 0.07f, 0.07f, 8, mat::metal, true, false);
      break;
    }
    case 6: {   // cone
      b.setTint({0.98f, 0.45f, 0.08f});
      b.frustum({0, 0.03f, 0}, {0, 0.7f, 0}, 0.17f, 0.04f, 10, mat::white, true, false);
      b.setTint({0.95f, 0.95f, 0.95f});
      b.frustum({0, 0.32f, 0}, {0, 0.44f, 0}, 0.105f, 0.085f, 10, mat::white, false, false);
      b.setTint({0.1f, 0.1f, 0.1f});
      lbox(b, -0.2f, 0, -0.2f, 0.2f, 0.035f, 0.2f, mat::metal);
      break;
    }
    case 7: {   // guarda-sol
      static const Vec3 pal[6] = {{0.95f, 0.15f, 0.1f}, {0.1f, 0.4f, 0.9f}, {0.98f, 0.8f, 0.1f}, {0.1f, 0.7f, 0.5f}, {0.95f, 0.45f, 0.1f}, {0.8f, 0.2f, 0.6f}};
      Vec3 c1 = pal[rng.irange(0, 5)], c2 = rng.chance(0.6f) ? Vec3{0.96f, 0.96f, 0.94f} : pal[rng.irange(0, 5)];
      float tx = rng.range(-0.12f, 0.12f), tz = rng.range(-0.12f, 0.12f);
      b.setTint({0.85f, 0.85f, 0.85f});
      b.frustum({0, 0, 0}, {tx, 2.35f, tz}, 0.03f, 0.022f, 6, mat::metal, true, false);
      const int n = 8;
      Vec3 apex{tx, 2.55f, tz};
      for (int i = 0; i < n; ++i) {
        float a0 = i * kTau / n, a1 = (i + 1) * kTau / n;
        Vec3 p0{tx * 0.8f + std::cos(a0) * 1.25f, 1.95f, tz * 0.8f + std::sin(a0) * 1.25f}, p1{tx * 0.8f + std::cos(a1) * 1.25f, 1.95f, tz * 0.8f + std::sin(a1) * 1.25f};
        b.setTint(i % 2 ? c2 : c1);
        Vec3 nrm = ((p0 + p1) * 0.5f - Vec3{tx, 1.4f, tz}).normalized();
        b.quad2(p0, p1, apex, apex, mat::white);
        (void)nrm;
      }
      break;
    }
    case 8: {   // cadeira de praia
      Vec3 c1 = rng.chance(0.5f) ? Vec3{0.2f, 0.45f, 0.9f} : Vec3{0.95f, 0.5f, 0.15f};
      b.setTint({0.85f, 0.85f, 0.85f});
      for (float x : {-0.28f, 0.28f}) {
        b.frustum({x, 0, -0.3f}, {x, 0.32f, 0.0f}, 0.015f, 0.015f, 5, mat::metal, false, false);
        b.frustum({x, 0, 0.3f}, {x, 0.32f, 0.0f}, 0.015f, 0.015f, 5, mat::metal, false, false);
        b.frustum({x, 0.32f, 0.0f}, {x, 0.78f, -0.36f}, 0.015f, 0.015f, 5, mat::metal, false, false);
      }
      for (int i = 0; i < 4; ++i) {
        b.setTint(i % 2 ? Vec3{0.96f, 0.96f, 0.94f} : c1);
        float t0 = i / 4.0f, t1 = (i + 1) / 4.0f;
        // back rest strips
        Vec3 a0 = lerp(Vec3{-0.3f, 0.32f, 0.0f}, Vec3{-0.3f, 0.8f, -0.38f}, t0), a1 = lerp(Vec3{-0.3f, 0.32f, 0.0f}, Vec3{-0.3f, 0.8f, -0.38f}, t1);
        Vec3 b0 = a0 + Vec3{0.6f, 0, 0}, b1 = a1 + Vec3{0.6f, 0, 0};
        b.quad2(a0, b0, b1, a1, mat::white);
        Vec3 s0 = lerp(Vec3{-0.3f, 0.32f, 0.0f}, Vec3{-0.3f, 0.26f, 0.45f}, t0), s1 = lerp(Vec3{-0.3f, 0.32f, 0.0f}, Vec3{-0.3f, 0.26f, 0.45f}, t1);
        b.quad2(s0, s0 + Vec3{0.6f, 0, 0}, s1 + Vec3{0.6f, 0, 0}, s1, mat::white);
      }
      break;
    }
    case 9: {   // quiosque com cobertura de palha
      b.setTint({0.7f, 0.55f, 0.38f});
      lbox(b, -1.6f, 0.0f, -1.6f, 1.6f, 0.15f, 1.6f, mat::wood, 1.0f);
      b.setTint({0.6f, 0.42f, 0.28f});
      for (float x : {-1.4f, 1.4f})
        for (float z : {-1.4f, 1.4f}) b.frustum({x, 0.15f, z}, {x, 2.5f, z}, 0.08f, 0.07f, 6, mat::wood, true, false, 0.8f);
      b.setTint({0.9f, 0.76f, 0.42f});
      b.frustum({0, 2.45f, 0}, {0, 3.7f, 0}, 2.5f, 0.12f, 10, mat::wood, true, false, 1.2f);
      b.setTint({0.8f, 0.66f, 0.34f});
      b.frustum({0, 2.38f, 0}, {0, 2.45f, 0}, 2.55f, 2.5f, 10, mat::wood, true, true, 1.2f);
      b.setTint({0.62f, 0.45f, 0.3f});
      lbox(b, -1.4f, 0.15f, 0.9f, 1.4f, 1.1f, 1.35f, mat::wood, 1.0f);
      b.setTint({0.98f, 0.98f, 0.96f});
      b.quad2({-1.0f, 1.5f, 1.45f}, {1.0f, 1.5f, 1.45f}, {1.0f, 2.1f, 1.45f}, {-1.0f, 2.1f, 1.45f}, mat::white);
      break;
    }
    case 10: {   // vaso com planta
      b.setTint({0.78f, 0.4f, 0.26f});
      b.frustum({0, 0, 0}, {0, 0.45f, 0}, 0.2f, 0.28f, 10, mat::wall_paint, true, true);
      b.setTint(jitter(rng, {0.4f, 0.7f, 0.3f}, 0.15f));
      b.blob({0, 0.7f, 0}, {0.38f, 0.32f, 0.38f}, 8, 4, 0.2f, rng.next(), mat::foliage, 0.55f);
      break;
    }
    case 11: {   // caixas de papelao empilhadas
      b.setTint(jitter(rng, {0.72f, 0.56f, 0.38f}, 0.08f));
      lbox(b, -0.4f, 0, -0.3f, 0.35f, 0.5f, 0.3f, mat::wood, 1.0f);
      b.setTint(jitter(rng, {0.7f, 0.54f, 0.36f}, 0.08f));
      lbox(b, 0.38f, 0, -0.25f, 0.9f, 0.38f, 0.25f, mat::wood, 1.0f);
      b.setTint(jitter(rng, {0.74f, 0.58f, 0.4f}, 0.08f));
      lbox(b, -0.3f, 0.5f, -0.22f, 0.25f, 0.85f, 0.25f, mat::wood, 1.0f);
      break;
    }
    case 12: {   // pilha de pneus
      for (int i = 0; i < 3; ++i) {
        b.setTint({0.09f, 0.09f, 0.1f});
        b.frustum({0, i * 0.2f, 0}, {0, i * 0.2f + 0.19f, 0}, 0.36f, 0.36f, 12, mat::metal, true, true);
        b.setTint({0.18f, 0.18f, 0.2f});
        b.frustum({0, i * 0.2f + 0.19f, 0}, {0, i * 0.2f + 0.195f, 0}, 0.2f, 0.2f, 10, mat::metal, true, false);
      }
      break;
    }
    case 13: {   // tambor
      Vec3 c = rng.chance(0.5f) ? Vec3{0.1f, 0.25f, 0.6f} : Vec3{0.7f, 0.12f, 0.08f};
      b.setTint(c);
      b.frustum({0, 0, 0}, {0, 0.9f, 0}, 0.29f, 0.29f, 12, mat::metal, true, true);
      b.setTint(c * 0.7f);
      for (float y : {0.25f, 0.62f}) b.frustum({0, y, 0}, {0, y + 0.04f, 0}, 0.305f, 0.305f, 12, mat::metal, false, false);
      break;
    }
    case 14: {   // placa de rua
      b.setTint({0.5f, 0.5f, 0.52f});
      b.frustum({0, 0, 0}, {0, 2.7f, 0}, 0.035f, 0.035f, 6, mat::metal, true, true);
      b.setTint({0.1f, 0.5f, 0.28f});
      lbox(b, -0.55f, 2.45f, -0.025f, 0.55f, 2.7f, 0.025f, mat::white);
      b.setTint({0.1f, 0.5f, 0.28f});
      lbox(b, -0.025f, 2.15f, -0.5f, 0.025f, 2.4f, 0.5f, mat::white);
      b.setTint({0.95f, 0.95f, 0.95f});
      lbox(b, -0.5f, 2.5f, 0.026f, 0.5f, 2.65f, 0.03f, mat::white);
      break;
    }
    case 15: {   // torre de salva-vidas
      b.setTint({0.9f, 0.88f, 0.84f});
      for (float x : {-0.8f, 0.8f})
        for (float z : {-0.8f, 0.8f}) b.frustum({x, 0, z}, {x * 0.85f, 1.9f, z * 0.85f}, 0.07f, 0.06f, 6, mat::wood, true, true);
      lbox(b, -1.0f, 1.85f, -1.0f, 1.0f, 1.95f, 1.0f, mat::wood, 1.0f);
      b.setTint({0.97f, 0.9f, 0.82f});
      lbox(b, -0.85f, 1.95f, -0.85f, 0.85f, 3.0f, 0.85f, mat::wall_paint, 1.5f);
      b.setTint({0.8f, 0.1f, 0.08f});
      lbox(b, -1.0f, 3.0f, -1.0f, 1.0f, 3.15f, 1.0f, mat::wall_paint, 1.5f);
      b.setTint({0.15f, 0.2f, 0.25f});
      lbox(b, -0.6f, 2.25f, 0.85f, 0.6f, 2.75f, 0.87f, mat::metal);
      b.setTint({0.9f, 0.75f, 0.5f});
      for (int i = 0; i < 6; ++i) lbox(b, -0.2f, 0.25f + i * 0.28f, 1.0f + i * 0.03f, 0.2f, 0.3f + i * 0.28f, 1.12f + i * 0.03f, mat::wood);
      b.setTint({0.6f, 0.6f, 0.62f});
      b.frustum({0.9f, 3.15f, 0}, {0.9f, 4.4f, 0}, 0.03f, 0.025f, 6, mat::metal, true, false);
      b.setTint({0.95f, 0.15f, 0.1f});
      b.quad2({0.9f, 4.35f, 0}, {0.9f, 4.35f, 0.9f}, {0.9f, 3.85f, 0.9f}, {0.9f, 3.85f, 0}, mat::white);
      break;
    }
    case 16: {   // semaforo
      b.setTint({0.2f, 0.2f, 0.22f});
      b.frustum({0, 0, 0}, {0, 3.6f, 0}, 0.07f, 0.05f, 8, mat::metal, true, true);
      lbox(b, -0.14f, 3.0f, -0.12f, 0.14f, 3.75f, 0.12f, mat::metal);
      const Vec3 lamp[3] = {{1.0f, 0.12f, 0.1f}, {1.0f, 0.75f, 0.1f}, {0.15f, 1.0f, 0.3f}};
      for (int i = 0; i < 3; ++i) {
        b.setTint(lamp[i]);
        b.setEmissive(i == 2 ? 0.9f : 0.18f);
        lbox(b, -0.07f, 3.52f - i * 0.24f, 0.12f, 0.07f, 3.66f - i * 0.24f, 0.135f, mat::white);
        lbox(b, -0.07f, 3.52f - i * 0.24f, -0.135f, 0.07f, 3.66f - i * 0.24f, -0.12f, mat::white);
      }
      b.setEmissive(0);
      break;
    }
    case 17: {   // caixa de correio
      b.setTint({0.3f, 0.3f, 0.33f});
      b.frustum({0, 0, 0}, {0, 1.0f, 0}, 0.04f, 0.04f, 6, mat::metal, true, true);
      b.setTint({0.1f, 0.3f, 0.7f});
      b.blob({0, 1.2f, 0}, {0.3f, 0.22f, 0.2f}, 10, 4, 0.0f, 11, mat::metal, 0.8f);
      break;
    }
    case 18: {   // ponto de onibus: abrigo com banco e placa
      b.setTint({0.35f, 0.36f, 0.4f});
      for (float x : {-1.4f, 1.4f}) b.frustum({x, 0, -0.5f}, {x, 2.5f, -0.5f}, 0.05f, 0.05f, 6, mat::metal, true, true);
      lbox(b, -1.6f, 2.5f, -0.7f, 1.6f, 2.58f, 0.7f, mat::metal);
      b.setTint({0.6f, 0.8f, 0.85f});
      b.quad2({-1.4f, 0.3f, -0.5f}, {1.4f, 0.3f, -0.5f}, {1.4f, 2.4f, -0.5f}, {-1.4f, 2.4f, -0.5f}, mat::white);
      b.setTint({0.62f, 0.42f, 0.25f});
      lbox(b, -1.0f, 0.42f, -0.4f, 1.0f, 0.47f, -0.05f, mat::wood);
      b.setTint({0.95f, 0.75f, 0.1f});
      b.frustum({1.9f, 0, 0.3f}, {1.9f, 2.6f, 0.3f}, 0.03f, 0.03f, 6, mat::metal, true, true);
      lbox(b, 1.7f, 2.3f, 0.28f, 2.1f, 2.65f, 0.32f, mat::white);
      break;
    }
    case 19: {   // outdoor
      static const Vec3 pal[5] = {{0.9f, 0.25f, 0.2f}, {0.2f, 0.5f, 0.9f}, {0.95f, 0.8f, 0.2f}, {0.25f, 0.75f, 0.45f}, {0.9f, 0.9f, 0.9f}};
      b.setTint({0.4f, 0.4f, 0.43f});
      for (float x : {-1.8f, 1.8f}) b.frustum({x, 0, 0}, {x, 4.2f, 0}, 0.1f, 0.08f, 8, mat::metal, true, true);
      b.setTint({0.15f, 0.15f, 0.17f});
      lbox(b, -2.6f, 3.9f, -0.12f, 2.6f, 6.6f, 0.0f, mat::metal);
      b.setTint(pal[rng.irange(0, 4)]);
      b.quad2({-2.45f, 4.05f, 0.01f}, {2.45f, 4.05f, 0.01f}, {2.45f, 6.45f, 0.01f}, {-2.45f, 6.45f, 0.01f}, mat::white);
      b.setTint({0.97f, 0.97f, 0.95f});
      b.quad2({-1.9f, 4.4f, 0.015f}, {1.0f, 4.4f, 0.015f}, {1.0f, 5.0f, 0.015f}, {-1.9f, 5.0f, 0.015f}, mat::white);
      break;
    }
    case 20: {   // placa PARE
      b.setTint({0.5f, 0.5f, 0.52f});
      b.frustum({0, 0, 0}, {0, 2.3f, 0}, 0.03f, 0.03f, 6, mat::metal, true, true);
      b.setTint({0.85f, 0.08f, 0.06f});
      b.frustum({0, 2.55f, 0}, {0, 2.55f, 0.03f}, 0.3f, 0.3f, 8, mat::white, true, true);
      b.setTint({0.97f, 0.97f, 0.97f});
      lbox(b, -0.2f, 2.5f, 0.032f, 0.2f, 2.6f, 0.04f, mat::white);
      break;
    }
    case 21: {   // carrinho de supermercado
      b.setTint({0.75f, 0.76f, 0.8f});
      lbox(b, -0.25f, 0.4f, -0.4f, 0.25f, 0.42f, 0.4f, mat::metal);
      for (float x : {-0.25f, 0.25f}) lbox(b, x - 0.01f, 0.42f, -0.4f, x + 0.01f, 0.85f, 0.4f, mat::metal);
      lbox(b, -0.25f, 0.83f, -0.4f, 0.25f, 0.87f, -0.38f, mat::metal);
      b.setTint({0.1f, 0.1f, 0.1f});
      for (float x : {-0.2f, 0.2f}) for (float z : {-0.35f, 0.35f}) b.frustum({x, 0.0f, z}, {x, 0.4f, z}, 0.03f, 0.03f, 5, mat::metal, false, false);
      break;
    }
    case 22: {   // banca de jornal
      b.setTint({0.2f, 0.5f, 0.3f});
      lbox(b, -1.2f, 0, -0.9f, 1.2f, 2.3f, 0.9f, mat::wall_paint, 1.5f);
      b.setTint({0.95f, 0.8f, 0.2f});
      lbox(b, -1.35f, 2.3f, -1.05f, 1.35f, 2.45f, 1.05f, mat::wall_paint, 1.5f);
      b.setTint({0.15f, 0.15f, 0.17f});
      lbox(b, -0.9f, 0.9f, 0.9f, 0.9f, 1.8f, 0.93f, mat::metal);
      static const Vec3 mag[4] = {{0.9f, 0.2f, 0.2f}, {0.2f, 0.5f, 0.9f}, {0.95f, 0.8f, 0.1f}, {0.9f, 0.9f, 0.9f}};
      for (int i = 0; i < 6; ++i) {
        b.setTint(mag[i % 4]);
        lbox(b, -0.85f + i * 0.28f, 1.0f + (i % 2) * 0.4f, 0.94f, -0.65f + i * 0.28f, 1.35f + (i % 2) * 0.4f, 0.95f, mat::white);
      }
      break;
    }
    case 23: {   // bicicleta apoiada
      b.setTint({0.15f, 0.15f, 0.17f});
      for (float z : {-0.5f, 0.5f}) {
        b.frustum({0, 0.34f, z - 0.06f}, {0, 0.34f, z + 0.06f}, 0.34f, 0.34f, 12, mat::metal, false, false);
        b.setTint({0.8f, 0.8f, 0.82f});
        b.frustum({0, 0.34f, z - 0.01f}, {0, 0.34f, z + 0.01f}, 0.3f, 0.3f, 12, mat::metal, true, true);
        b.setTint({0.15f, 0.15f, 0.17f});
      }
      b.setTint(jitter(rng, {0.8f, 0.2f, 0.15f}, 0.3f));
      b.frustum({0, 0.34f, -0.5f}, {0, 0.62f, -0.1f}, 0.02f, 0.02f, 5, mat::metal, false, false);
      b.frustum({0, 0.62f, -0.1f}, {0, 0.34f, 0.5f}, 0.02f, 0.02f, 5, mat::metal, false, false);
      b.frustum({0, 0.62f, -0.1f}, {0, 0.95f, -0.05f}, 0.02f, 0.02f, 5, mat::metal, false, false);
      lbox(b, -0.2f, 0.95f, -0.08f, 0.2f, 0.97f, -0.02f, mat::metal);
      break;
    }
    case 24: {   // toalha de praia
      static const Vec3 pal[5] = {{0.95f, 0.3f, 0.3f}, {0.3f, 0.6f, 0.95f}, {0.98f, 0.85f, 0.2f}, {0.4f, 0.85f, 0.6f}, {0.95f, 0.55f, 0.8f}};
      b.setTint(pal[rng.irange(0, 4)]);
      b.quad({-0.45f, 0.03f, 0.9f}, {0.45f, 0.03f, 0.9f}, {0.45f, 0.03f, -0.9f}, {-0.45f, 0.03f, -0.9f}, {0, 0}, {1, 0}, {1, 1}, {0, 1}, mat::white);
      b.setTint({0.97f, 0.97f, 0.95f});
      b.quad({-0.45f, 0.032f, 0.2f}, {0.45f, 0.032f, 0.2f}, {0.45f, 0.032f, 0.0f}, {-0.45f, 0.032f, 0.0f}, {0, 0}, {1, 0}, {1, 1}, {0, 1}, mat::white);
      break;
    }
    case 25: {   // prancha de surf fincada na areia
      static const Vec3 pal[4] = {{0.2f, 0.7f, 0.95f}, {0.98f, 0.8f, 0.15f}, {0.95f, 0.3f, 0.4f}, {0.95f, 0.95f, 0.9f}};
      b.setTint(pal[rng.irange(0, 3)]);
      b.blob({0, 0.95f, 0}, {0.26f, 1.0f, 0.045f}, 10, 6, 0.0f, 2, mat::white, 0.9f);
      break;
    }
    default: {   // 26: canteiro com flores
      b.setTint({0.75f, 0.74f, 0.7f});
      lbox(b, -0.9f, 0, -0.45f, 0.9f, 0.3f, 0.45f, mat::concrete, 1.0f);
      b.setTint({0.35f, 0.25f, 0.15f});
      b.quad({-0.85f, 0.31f, 0.4f}, {0.85f, 0.31f, 0.4f}, {0.85f, 0.31f, -0.4f}, {-0.85f, 0.31f, -0.4f}, {0, 0}, {1, 0}, {1, 1}, {0, 1}, mat::dirt);
      for (int i = 0; i < 6; ++i) {
        b.setTint(rng.chance(0.5f) ? jitter(rng, {0.95f, 0.3f, 0.45f}, 0.2f) : jitter(rng, {0.98f, 0.85f, 0.2f}, 0.1f));
        b.blob({-0.7f + i * 0.28f, 0.5f, rng.range(-0.25f, 0.25f)}, {0.17f, 0.14f, 0.17f}, 6, 3, 0.15f, rng.next(), mat::foliage, 0.7f);
      }
      break;
    }
  }
  b.clearXform();
  b.setTint({1, 1, 1});
}

}  // namespace gtabr
