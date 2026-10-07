// Sprite baker: renders procedural 3D models from many directions into packed RGBA atlases.
// Usage: spritebake <outdir> [--only vehicles|chars|trees|props] [--quick]
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/png.h"
#include "models.h"

using namespace bake;

namespace {
struct Job {
  std::string atlas;
  std::string name;
  const Mesh* mesh;
  float yaw;
  BakeView view;
  float lightYawBias = 0;
};
struct Sprite {
  std::string atlas, name;
  Image img;
  float pivotX, pivotY, ppm;
  int page = 0, x = 0, y = 0;
};

float encode(float v) {
  // soft shoulder then sRGB-ish gamma
  if (v > 0.8f) v = 0.8f + 0.2f * (1.0f - std::exp(-(v - 0.8f) / 0.2f));
  v = std::max(0.0f, std::min(1.0f, v));
  return std::pow(v, 1.0f / 2.2f);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: spritebake <outdir> [--only X] [--quick]\n"); return 1; }
  std::string outDir = argv[1];
  std::string only;
  bool quick = false;
  for (int i = 2; i < argc; ++i) {
    if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
    if (!strcmp(argv[i], "--quick")) quick = true;
  }
  auto want = [&](const char* g) { return only.empty() || only == g; };
  const int SS = quick ? 2 : 3;
  const float PITCH_LOW = 17.0f, PITCH_HIGH = 66.0f;

  auto mats = makeMaterials();
  Lighting L;
  std::vector<std::unique_ptr<Mesh>> meshes;
  std::vector<Job> jobs;
  auto keep = [&](Mesh&& m) { meshes.push_back(std::make_unique<Mesh>(std::move(m))); return meshes.back().get(); };

  // ---------------- vehicles
  if (want("vehicles")) {
    struct VC { const char* model; const char* color; Vec3 rgb; };
    const VC vcs[] = {
        {"compacto", "branco", {0.80f, 0.80f, 0.79f}}, {"compacto", "vermelho", {0.62f, 0.05f, 0.05f}}, {"compacto", "prata", {0.50f, 0.52f, 0.55f}},
        {"sedan", "grafite", {0.085f, 0.09f, 0.105f}}, {"sedan", "branco", {0.80f, 0.80f, 0.79f}}, {"sedan", "azul", {0.05f, 0.13f, 0.38f}},
        {"picape", "azul", {0.06f, 0.22f, 0.46f}}, {"picape", "branco", {0.80f, 0.80f, 0.79f}}, {"picape", "vinho", {0.26f, 0.025f, 0.055f}}};
    for (const VC& v : vcs) {
      const Mesh* mesh = keep(buildVehicle(v.model, v.rgb));
      std::string base = std::string("veh_") + v.model + "_" + v.color;
      const int nLow = 16, nHigh = 32;
      for (int d = 0; d < nLow; ++d)
        jobs.push_back({"veh_low", base + "_" + std::to_string(d), mesh, d * 2 * kPi / nLow, {PITCH_LOW, 96.0f, SS}});
      for (int d = 0; d < nHigh; ++d)
        jobs.push_back({"veh_high", base + "_" + std::to_string(d), mesh, d * 2 * kPi / nHigh, {PITCH_HIGH, 50.0f, SS}});
    }
  }

  // ---------------- characters
  struct CharEntry { CharSpec spec; int dirs; bool run; };
  std::vector<CharEntry> chars;
  if (want("chars")) {
    CharSpec p; p.name = "player"; p.skin = {0.62f, 0.40f, 0.29f}; p.hair = {0.03f, 0.025f, 0.02f}; p.hairStyle = 0;
    p.shirt = {0.82f, 0.58f, 0.04f}; p.pants = {0.12f, 0.17f, 0.30f}; p.shoes = {0.85f, 0.85f, 0.85f};
    chars.push_back({p, 16, true});
    CharSpec f; f.name = "frentista"; f.skin = {0.68f, 0.46f, 0.34f}; f.hair = {0.04f, 0.03f, 0.03f}; f.shirt = {0.07f, 0.16f, 0.50f};
    f.pants = {0.09f, 0.10f, 0.16f}; f.shoes = {0.05f, 0.05f, 0.05f}; f.hat = 1; f.hatCol = {0.08f, 0.17f, 0.55f}; f.moustache = true;
    chars.push_back({f, 8, false});
    CharSpec a; a.name = "atendente"; a.skin = {0.72f, 0.5f, 0.38f}; a.hair = {0.05f, 0.03f, 0.025f}; a.hairStyle = 5;
    a.shirt = {0.85f, 0.85f, 0.85f}; a.pants = {0.1f, 0.1f, 0.12f}; a.apron = true; a.apronCol = {0.65f, 0.06f, 0.06f}; a.shoes = {0.1f, 0.1f, 0.1f};
    a.girth = 0.92f; a.scale = 0.95f;
    chars.push_back({a, 8, false});
    CharSpec m; m.name = "mecanico"; m.skin = {0.55f, 0.36f, 0.25f}; m.hair = {0.04f, 0.04f, 0.04f}; m.hairStyle = 3; m.beard = true;
    m.shirt = {0.28f, 0.30f, 0.33f}; m.pants = {0.2f, 0.22f, 0.25f}; m.shoes = {0.08f, 0.07f, 0.06f}; m.longSleeves = true; m.girth = 1.12f;
    m.hat = 1; m.hatCol = {0.55f, 0.1f, 0.06f};
    chars.push_back({m, 8, false});
    CharSpec v; v.name = "vizinho"; v.skin = {0.8f, 0.6f, 0.47f}; v.hair = {0.85f, 0.85f, 0.83f}; v.hairStyle = 3; v.moustache = true; v.hat = 2;
    v.hatCol = {0.4f, 0.1f, 0.08f}; v.shirt = {0.35f, 0.18f, 0.12f}; v.pants = {0.38f, 0.33f, 0.26f}; v.shoes = {0.2f, 0.12f, 0.08f};
    v.longSleeves = true; v.scale = 0.96f; v.girth = 1.08f;
    chars.push_back({v, 8, false});
    CharSpec n1; n1.name = "mulher_rosa"; n1.skin = {0.75f, 0.52f, 0.4f}; n1.hair = {0.07f, 0.04f, 0.03f}; n1.hairStyle = 1; n1.shirt = {0.82f, 0.28f, 0.45f};
    n1.pants = {0.18f, 0.25f, 0.42f}; n1.shoes = {0.9f, 0.9f, 0.9f}; n1.scale = 0.96f; n1.girth = 0.94f; n1.bag = true;
    chars.push_back({n1, 8, false});
    CharSpec n2; n2.name = "homem_polo"; n2.skin = {0.88f, 0.68f, 0.55f}; n2.hair = {0.25f, 0.15f, 0.08f}; n2.hairStyle = 0; n2.shirt = {0.16f, 0.45f, 0.30f};
    n2.pants = {0.55f, 0.48f, 0.36f}; n2.shoes = {0.3f, 0.2f, 0.12f}; n2.scale = 1.03f;
    chars.push_back({n2, 8, false});
    CharSpec n3; n3.name = "jovem_moletom"; n3.skin = {0.45f, 0.3f, 0.22f}; n3.hair = {0.03f, 0.03f, 0.03f}; n3.hairStyle = 4; n3.shirt = {0.35f, 0.35f, 0.4f};
    n3.pants = {0.08f, 0.08f, 0.1f}; n3.shoes = {0.9f, 0.2f, 0.15f}; n3.longSleeves = true; n3.backpack = true; n3.scale = 0.98f;
    chars.push_back({n3, 8, false});
    CharSpec n4; n4.name = "mulher_vestido"; n4.skin = {0.9f, 0.7f, 0.58f}; n4.hair = {0.45f, 0.28f, 0.1f}; n4.hairStyle = 2; n4.shirt = {0.9f, 0.82f, 0.25f};
    n4.pants = {0.9f, 0.82f, 0.25f}; n4.skirt = true; n4.shoes = {0.15f, 0.1f, 0.1f}; n4.scale = 0.95f; n4.girth = 0.93f;
    chars.push_back({n4, 8, false});
    CharSpec n5; n5.name = "corredor"; n5.skin = {0.78f, 0.57f, 0.43f}; n5.hair = {0.1f, 0.07f, 0.04f}; n5.hairStyle = 0; n5.shirt = {0.1f, 0.55f, 0.75f};
    n5.pants = {0.08f, 0.08f, 0.1f}; n5.shorts = true; n5.shoes = {0.95f, 0.95f, 0.2f}; n5.scale = 1.01f;
    chars.push_back({n5, 8, false});
    for (auto& ce : chars) {
      auto addAnim = [&](Gait g, const char* tag, int frames) {
        for (int fr = 0; fr < frames; ++fr) {
          const Mesh* mesh = keep(buildCharacter(ce.spec, g, (float)fr / std::max(1, frames)));
          std::string base = "chr_" + ce.spec.name + "_" + tag + std::to_string(fr);
          for (int d = 0; d < ce.dirs; ++d) {
            jobs.push_back({"chr_low", base + "_" + std::to_string(d), mesh, d * 2 * kPi / ce.dirs, {PITCH_LOW, 120.0f, SS}});
            jobs.push_back({"chr_high", base + "_" + std::to_string(d), mesh, d * 2 * kPi / ce.dirs, {PITCH_HIGH, 64.0f, SS}});
          }
        }
      };
      addAnim(Gait::Idle, "idle", 1);
      addAnim(Gait::Walk, "walk", 8);
      if (ce.run) addAnim(Gait::Run, "run", 8);
    }
  }

  // ---------------- trees / shrubs
  if (want("trees")) {
    const char* tn[3] = {"arvore", "palmeira", "arbusto"};
    for (int type = 0; type < 3; ++type)
      for (int variant = 0; variant < 2; ++variant) {
        const Mesh* mesh = keep(buildTree(type, 10 + type * 10 + variant));
        std::string base = std::string("tree_") + tn[type] + std::to_string(variant);
        float ppmLow = type == 2 ? 100.0f : 56.0f, ppmHigh = type == 2 ? 60.0f : 36.0f;
        for (int d = 0; d < 4; ++d) {
          jobs.push_back({"prp_low", base + "_" + std::to_string(d), mesh, d * kPi / 2, {PITCH_LOW, ppmLow, SS}});
          jobs.push_back({"prp_high", base + "_" + std::to_string(d), mesh, d * kPi / 2, {PITCH_HIGH, ppmHigh, SS}});
        }
      }
  }
  // ---------------- props
  if (want("props")) {
    const char* names[] = {"lixeira", "cone", "caixas", "banco", "orelhao", "pneus", "tambor", "hidrante", "vaso", "bomba", "poste_placa"};
    for (const char* n : names) {
      const Mesh* mesh = keep(buildProp(n));
      for (int d = 0; d < 8; ++d) {
        jobs.push_back({"prp_low", std::string("prop_") + n + "_" + std::to_string(d), mesh, d * kPi / 4, {PITCH_LOW, 110.0f, SS}});
        jobs.push_back({"prp_high", std::string("prop_") + n + "_" + std::to_string(d), mesh, d * kPi / 4, {PITCH_HIGH, 70.0f, SS}});
      }
    }
  }

  printf("baking %zu sprites (%zu meshes) with %d threads\n", jobs.size(), meshes.size(), (int)std::thread::hardware_concurrency());
  std::vector<Sprite> sprites(jobs.size());
  std::atomic<size_t> next{0};
  auto worker = [&]() {
    for (;;) {
      size_t i = next.fetch_add(1);
      if (i >= jobs.size()) return;
      const Job& j = jobs[i];
      BakeResult r = bakeMesh(*j.mesh, mats, j.view, j.yaw, L);
      Sprite& s = sprites[i];
      s.atlas = j.atlas; s.name = j.name; s.img = std::move(r.img); s.pivotX = r.pivotX; s.pivotY = r.pivotY; s.ppm = j.view.ppm;
      for (size_t p = 0; p < s.img.rgba.size(); p += 4)
        for (int c = 0; c < 3; ++c) s.img.rgba[p + c] = encode(s.img.rgba[p + c]);
      if (i % 200 == 0) { printf("  %zu/%zu\n", i, jobs.size()); fflush(stdout); }
    }
  };
  std::vector<std::thread> th;
  unsigned nt = std::max(1u, std::thread::hardware_concurrency());
  for (unsigned i = 0; i < nt; ++i) th.emplace_back(worker);
  for (auto& t : th) t.join();

  // ---------------- pack per atlas
  const int PAGE = 4096, PAD = 3;
  std::map<std::string, std::vector<size_t>> byAtlas;
  for (size_t i = 0; i < sprites.size(); ++i) byAtlas[sprites[i].atlas].push_back(i);
  std::string meta;
  char line[512];
  for (auto& [atlas, ids] : byAtlas) {
    std::sort(ids.begin(), ids.end(), [&](size_t a, size_t b) { return sprites[a].img.h > sprites[b].img.h; });
    std::vector<std::vector<uint8_t>> pages;
    std::vector<int> pageH;
    int curPage = -1, shelfY = 0, shelfH = 0, cx = 0;
    auto newPage = [&]() { pages.emplace_back((size_t)PAGE * PAGE * 4, 0); pageH.push_back(0); ++curPage; shelfY = 0; shelfH = 0; cx = 0; };
    newPage();
    for (size_t id : ids) {
      Sprite& s = sprites[id];
      int w = s.img.w + PAD * 2, h = s.img.h + PAD * 2;
      if (w > PAGE || h > PAGE) { fprintf(stderr, "sprite too large: %s (%dx%d)\n", s.name.c_str(), w, h); continue; }
      if (cx + w > PAGE) { shelfY += shelfH; shelfH = 0; cx = 0; }
      if (shelfY + h > PAGE) newPage();
      s.page = curPage; s.x = cx + PAD; s.y = shelfY + PAD;
      cx += w; shelfH = std::max(shelfH, h);
      pageH[curPage] = std::max(pageH[curPage], shelfY + shelfH);
      auto& pg = pages[curPage];
      for (int y = 0; y < s.img.h; ++y)
        for (int x = 0; x < s.img.w; ++x) {
          const float* src = &s.img.rgba[((size_t)y * s.img.w + x) * 4];
          uint8_t* dst = &pg[((size_t)(s.y + y) * PAGE + (s.x + x)) * 4];
          for (int c = 0; c < 4; ++c) dst[c] = (uint8_t)(std::max(0.0f, std::min(1.0f, src[c])) * 255.0f + 0.5f);
        }
    }
    for (size_t p = 0; p < pages.size(); ++p) {
      int h = 64;
      while (h < pageH[p]) h *= 2;  // power-of-two height keeps mip chains clean
      h = std::min(h, PAGE);
      std::string path = outDir + "/" + atlas + "_" + std::to_string(p) + ".rgba";
      FILE* f = fopen(path.c_str(), "wb");
      fwrite(pages[p].data(), 1, (size_t)PAGE * h * 4, f);
      fclose(f);
      snprintf(line, sizeof(line), "PAGE %s %zu %d %d\n", atlas.c_str(), p, PAGE, h);
      meta += line;
      printf("%s page %zu: %dx%d\n", atlas.c_str(), p, PAGE, h);
      std::string png = outDir + "/" + atlas + "_" + std::to_string(p) + "_preview.png";
      gtabr::writePng(png, pages[p].data(), PAGE, h);
    }
    // sprite records need the final page height for v coordinates
    for (size_t id : ids) {
      Sprite& s = sprites[id];
      int h = 64;
      while (h < pageH[s.page]) h *= 2;
      h = std::min(h, PAGE);
      float u0 = (float)s.x / PAGE, u1 = (float)(s.x + s.img.w) / PAGE, v0 = (float)s.y / h, v1 = (float)(s.y + s.img.h) / h;
      snprintf(line, sizeof(line), "S %s %d %s %.6f %.6f %.6f %.6f %.5f %.5f %.4f %.4f\n", atlas.c_str(), s.page, s.name.c_str(), u0, v0, u1, v1,
               s.pivotX / s.img.w, s.pivotY / s.img.h, s.img.w / s.ppm, s.img.h / s.ppm);
      meta += line;
    }
  }
  std::string mp = outDir + "/sprites.txt";
  FILE* f = fopen(mp.c_str(), "wb");
  fwrite(meta.data(), 1, meta.size(), f);
  fclose(f);
  printf("done: %zu sprites\n", sprites.size());
  return 0;
}
