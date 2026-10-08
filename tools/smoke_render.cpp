// Smoke test: headless Vulkan init, draws a lit box + ground + UI, writes smoke.png.
#include <cstdio>

#include "core/png.h"
#include "gfx/renderer.h"

using namespace gtabr;
using namespace gtabr::gfx;

static void addBox(std::vector<WorldVertex>& v, std::vector<uint32_t>& idx, Vec3 mn, Vec3 mx, uint32_t color, float layer) {
  struct F { Vec3 n; Vec3 c[4]; };
  Vec3 a = mn, b = mx;
  F faces[6] = {
      {{0, 1, 0}, {{a.x, b.y, b.z}, {b.x, b.y, b.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z}}},
      {{0, -1, 0}, {{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, a.y, b.z}, {a.x, a.y, b.z}}},
      {{0, 0, 1}, {{a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}}},
      {{0, 0, -1}, {{b.x, a.y, a.z}, {a.x, a.y, a.z}, {a.x, b.y, a.z}, {b.x, b.y, a.z}}},
      {{1, 0, 0}, {{b.x, a.y, b.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {b.x, b.y, b.z}}},
      {{-1, 0, 0}, {{a.x, a.y, a.z}, {a.x, a.y, b.z}, {a.x, b.y, b.z}, {a.x, b.y, a.z}}}};
  for (auto& f : faces) {
    uint32_t base = (uint32_t)v.size();
    float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i) {
      WorldVertex w{};
      w.p[0] = f.c[i].x; w.p[1] = f.c[i].y; w.p[2] = f.c[i].z;
      w.n[0] = (int8_t)(f.n.x * 127); w.n[1] = (int8_t)(f.n.y * 127); w.n[2] = (int8_t)(f.n.z * 127);
      w.uv[0] = uvs[i][0]; w.uv[1] = uvs[i][1];
      w.color = color; w.layer = layer;
      v.push_back(w);
    }
    for (int i : {0, 1, 2, 0, 2, 3}) idx.push_back(base + i);
  }
}

int main() {
  RendererConfig cfg;
  cfg.headless = true;
  cfg.validation = getenv("VVL") != nullptr;
  Renderer r;
  if (!r.init(cfg, nullptr, {})) { LOGE("init failed"); return 1; }
  std::vector<WorldVertex> v; std::vector<uint32_t> idx;
  uint32_t grey = packRGBA8(0.45f, 0.45f, 0.47f, 1.0f), red = packRGBA8(0.8f, 0.3f, 0.25f, 1.0f);
  addBox(v, idx, {-40, -0.5f, -40}, {40, 0, 40}, grey, 0);
  addBox(v, idx, {-3, 0, -3}, {3, 6, 3}, red, 0);
  MeshHandle m = r.createMesh(v.data(), v.size(), idx.data(), idx.size());
  uint8_t px[4 * 4];
  for (int i = 0; i < 16; ++i) px[i] = 255;
  TexHandle mat = r.createTextureRGBA(2, 2, px, true, false, SamplerKind::Repeat);

  FrameData fd;
  fd.worldMaterial = r.createWorldMaterial({}, {});
  Vec3 eye{14, 11, 16}, tgt{0, 1, 0};
  Mat4 view = Mat4::lookAt(eye, tgt, {0, 1, 0});
  Mat4 proj = Mat4::perspective(50 * kDeg2Rad, 1280.0f / 576.0f, 0.3f, 400.0f);
  Vec3 sun = Vec3{-0.5f, 0.8f, 0.4f}.normalized();
  Mat4 lview = Mat4::lookAt(tgt + sun * 60.0f, tgt, {0, 1, 0});
  Mat4 lproj = Mat4::ortho(-40, 40, -40, 40, 1, 140);
  GlobalsUBO& g = fd.globals;
  g.view = view; g.viewProj = proj * view;
  g.lightViewProj[0] = g.lightViewProj[1] = lproj * lview;
  g.cascade = {0, 1, 0.05f, 0};
  g.sky0 = {0.12f, 0.28f, 0.74f, 0.4f}; g.sky1 = {0.62f, 0.72f, 0.86f, 1.0f};
  Mat4 inv = view; (void)inv;
  g.camPos = {eye.x, eye.y, eye.z, 0};
  g.camRight = {1, 0, 0, 0}; g.camUp = {0, 1, 0, 0}; g.camFwd = {0, 0, -1, 0};
  g.sunDir = {sun.x, sun.y, sun.z, 1.0f};
  g.sunColor = {3.2f, 2.8f, 2.3f, 0}; g.ambSky = {0.5f, 0.6f, 0.8f, 0}; g.ambGround = {0.25f, 0.22f, 0.2f, 0};
  g.fog = {0.7f, 0.78f, 0.88f, 0.004f}; g.params = {1.0f, 1.0f / 2048.0f, 0, 0};
  fd.worldMeshes = {m.id}; fd.shadowMeshes = {m.id};
  // UI rect
  UiInst u{};
  u.rect[0] = 40; u.rect[1] = 40; u.rect[2] = 220; u.rect[3] = 60;
  u.color = packRGBA8(0.05f, 0.05f, 0.08f, 0.7f); u.color2 = u.color; u.radius = 14; u.kind = kUiRect;
  fd.ui.push_back(u);
  fd.uiBatches.push_back({mat, 0, 1});
  r.requestReadback();
  for (int i = 0; i < 3; ++i) {
    if (i == 2) r.requestReadback();
    r.renderFrame(fd);
  }
  std::vector<uint8_t> rgba; uint32_t w, h;
  if (!r.readback(rgba, w, h)) { LOGE("readback failed"); return 2; }
  writePng("smoke.png", rgba.data(), w, h);
  LOGI("wrote smoke.png %ux%u", w, h);
  r.shutdown();
  return 0;
}
