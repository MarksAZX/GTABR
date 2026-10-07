// Headless desktop runner: renders the game offscreen (Vulkan, works with software ICDs), optionally drives a scripted
// scenario and writes screenshots. Used for development, visual verification and automated checks.
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../core/fileio.h"
#include "../core/jobs.h"
#include "../core/log.h"
#include "../core/png.h"
#include "../game/game.h"

using namespace gtabr;

int runScenario(const std::string& name, Game& game, gfx::Renderer& r, gfx::FrameData& fd, const std::string& out, float dt);

namespace {
struct Args {
  std::string out = "shots";
  std::string scenario = "start";
  std::string assets = "assets";
  std::string save = "build/save";
  int width = 1600, height = 720;
  bool newGame = true;
  bool validation = false;
  float scale = 1.0f;
};

void screenshot(gfx::Renderer& r, const std::string& path) {
  std::vector<uint8_t> px;
  uint32_t w, h;
  if (r.readback(px, w, h)) {
    writePng(path, px.data(), w, h);
    LOGI("screenshot %s", path.c_str());
  }
}
}  // namespace

int main(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    std::string s = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (s == "--out") a.out = next();
    else if (s == "--scenario") a.scenario = next();
    else if (s == "--assets") a.assets = next();
    else if (s == "--save") a.save = next();
    else if (s == "--width") a.width = std::atoi(next().c_str());
    else if (s == "--height") a.height = std::atoi(next().c_str());
    else if (s == "--continue") a.newGame = false;
    else if (s == "--validation") a.validation = true;
    else if (s == "--scale") a.scale = (float)std::atof(next().c_str());
  }
  std::string mk = "mkdir -p '" + a.out + "' '" + a.save + "'";
  if (std::system(mk.c_str()) != 0) return 2;
  fileio::setAssetRoot(a.assets);

  gfx::RendererConfig rc;
  rc.headless = true;
  rc.headlessWidth = (uint32_t)a.width;
  rc.headlessHeight = (uint32_t)a.height;
  rc.validation = a.validation;
  rc.renderScale = a.scale;
  gfx::Renderer r;
  if (!r.init(rc, nullptr, {})) return 1;
  JobSystem jobs(3);
  Game game;
  Game::Init gi;
  gi.renderer = &r;
  gi.jobs = &jobs;
  gi.saveDir = a.save;
  gi.newGame = a.newGame;
  game.init(gi);
  game.setScreenSize((float)a.width, (float)a.height);

  gfx::FrameData fd;
  const float dt = 1.0f / 30.0f;
  int frame = 0;
  // loading
  while (!game.loaded() && frame < 3000) {
    game.frame(dt, fd);
    r.renderFrame(fd);
    ++frame;
    if (frame % 30 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (!game.loaded()) { LOGE("game failed to load"); return 3; }
  LOGI("loaded after %d frames", frame);


  int rc2 = runScenario(a.scenario, game, r, fd, a.out, dt);
  game.shutdown();
  r.shutdown();
  return rc2;
}
