// Scripted scenarios for the headless runner.
#include <cmath>
#include <string>

#include "../core/log.h"
#include "../core/png.h"
#include "../game/game.h"

using namespace gtabr;

namespace {
void shot(gfx::Renderer& r, const std::string& path) {
  std::vector<uint8_t> px;
  uint32_t w, h;
  if (r.readback(px, w, h)) { writePng(path, px.data(), w, h); LOGI("screenshot %s", path.c_str()); }
}
void run(Game& g, gfx::Renderer& r, gfx::FrameData& fd, float dt, int frames, const std::string& shotPath = "") {
  for (int i = 0; i < frames; ++i) {
    bool last = i == frames - 1 && !shotPath.empty();
    if (last) r.requestReadback();
    g.frame(dt, fd);
    r.renderFrame(fd);
    if (last) shot(r, shotPath);
  }
}
}  // namespace

int runScenario(const std::string& name, Game& g, gfx::Renderer& r, gfx::FrameData& fd, const std::string& out, float dt) {
  if (name == "start") {
    run(g, r, fd, dt, 60, out + "/01_topdown.png");
    g.toggleCamera();
    run(g, r, fd, dt, 60, out + "/02_third.png");
    return 0;
  }
  return 0;
}
