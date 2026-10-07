// Scripted scenarios for the headless runner. "mvp" plays the whole target loop with real (injected) touch input:
// walk -> switch camera -> enter car -> drive -> refuel at the pump -> market (talk + buy) -> workshop repair -> save -> reload.
#include <cmath>
#include <string>

#include "../core/log.h"
#include "../core/png.h"
#include "../game/game.h"

using namespace gtabr;

namespace {
int g_failures = 0;
#define CHECK(cond, msg)                                           \
  do {                                                             \
    if (!(cond)) { ++g_failures; LOGE("CHECK FAILED: %s", msg); }  \
    else LOGI("ok: %s", msg);                                      \
  } while (0)

struct Bot {
  Game& g;
  gfx::Renderer& r;
  gfx::FrameData& fd;
  float dt;
  std::string outDir;
  int frames = 0;

  void shot(const std::string& name) {
    r.requestReadback();
    g.frame(dt, fd);
    r.renderFrame(fd);
    std::vector<uint8_t> px;
    uint32_t w, h;
    if (r.readback(px, w, h)) writePng(outDir + "/" + name + ".png", px.data(), w, h);
    ++frames;
  }
  void step(const InputFrame& in, int n = 1) {
    for (int i = 0; i < n; ++i) {
      g.injectInput(in);
      g.frame(dt, fd);
      r.renderFrame(fd);
      ++frames;
    }
  }
  void idle(int n) { InputFrame in; step(in, n); }
  void press(bool InputFrame::*field) {
    InputFrame in;
    in.*field = true;
    step(in, 1);
    idle(2);
  }
  bool walkTo(Vec2 target, float tol, int maxFrames, bool run = false) {
    for (int i = 0; i < maxFrames; ++i) {
      Vec2 d = target - g.player().pos;
      if (d.length() < tol) { idle(2); return true; }
      float yaw = g.camera().yaw();
      Vec2 f{std::sin(yaw), -std::cos(yaw)}, rt{std::cos(yaw), std::sin(yaw)};
      Vec2 n = d.normalized();
      InputFrame in;
      in.move = {n.dot(rt), n.dot(f)};
      in.runHeld = run;
      step(in, 1);
    }
    return false;
  }
  bool driveTo(Vec2 target, float tol, int maxFrames, float maxSpeed = 12.0f) {
    for (int i = 0; i < maxFrames; ++i) {
      Vehicle& v = g.vehicles()[g.player().vehicle];
      Vec2 d = target - v.pos;
      float dist = d.length();
      if (dist < tol) return true;
      float err = wrapAngle(yawFromDir(d) - v.yaw);
      float steer = clamp(err * 2.2f, -1.0f, 1.0f);
      float tgt = clamp(dist * 0.7f, 2.5f, maxSpeed) * clamp(1.0f - std::fabs(err) / 1.6f, 0.25f, 1.0f);
      float thr = clamp((tgt - v.speed) * 0.5f, -1.0f, 1.0f);
      InputFrame in;
      in.move = {steer, thr};
      step(in, 1);
    }
    return false;
  }
  bool stopCar(int maxFrames = 120) {
    for (int i = 0; i < maxFrames; ++i) {
      Vehicle& v = g.vehicles()[g.player().vehicle];
      if (std::fabs(v.speed) < 0.25f) return true;
      InputFrame in;
      in.move = {0, v.speed > 0 ? -1.0f : 1.0f};
      step(in, 1);
    }
    return false;
  }
};
}  // namespace

int runScenario(const std::string& name, Game& g, gfx::Renderer& r, gfx::FrameData& fd, const std::string& out, float dt) {
  Bot b{g, r, fd, dt, out};
  if (name == "start") {
    b.idle(40);
    b.shot("01_topdown");
    g.toggleCamera();
    b.idle(45);
    b.shot("02_third");
    return 0;
  }
  if (name == "char") {
    g.toggleCamera();
    b.idle(60);
    b.shot("char_idle");
    InputFrame in;
    in.move = {0, -1};
    b.step(in, 20);
    b.shot("char_walk");
    in.runHeld = true;
    b.step(in, 25);
    b.shot("char_run");
    return 0;
  }
  if (name == "look") {
    // visual review: both cameras at morning, afternoon, dusk and night
    const float hours[4] = {8.5f, 15.5f, 17.9f, 21.5f};
    const char* tags[4] = {"manha", "tarde", "por_do_sol", "noite"};
    b.idle(30);
    for (int i = 0; i < 4; ++i) {
      g.setTimeOfDay(hours[i], 0.0f);
      b.idle(4);
      b.shot(std::string("look_") + tags[i] + "_topdown");
      g.toggleCamera();
      b.idle(45);
      b.shot(std::string("look_") + tags[i] + "_third");
      g.toggleCamera();
      b.idle(45);
    }
    return 0;
  }
  if (name == "mvp") {
    // ---- walk to the car
    b.idle(20);
    b.shot("10_spawn_topdown");
    Vec2 carPos = g.vehicles()[0].pos;
    CHECK(b.walkTo(carPos + Vec2{0, 2.2f}, 0.9f, 400), "walked towards the parked car");
    // ---- camera toggle (smooth transition) and back
    g.toggleCamera();
    b.idle(12);
    b.shot("11_camera_midtransition");
    b.idle(40);
    CHECK(g.camera().mode() == CamMode::ThirdPerson && g.camera().blend() > 0.99f, "camera switched to third person");
    b.shot("12_third_person");
    g.toggleCamera();
    b.idle(60);
    CHECK(g.camera().blend() < 0.01f, "camera switched back to top down");
    // ---- enter the car
    InputFrame in;
    in.enterExitPressed = true;
    b.step(in, 1);
    b.idle(30);
    CHECK(g.player().vehicle == 0, "entered the car");
    b.shot("13_in_car_topdown");
    // ---- drive to the gas station
    Vehicle& car = g.vehicles()[0];
    float fuel0 = car.fuel;
    CHECK(b.driveTo({-4.0f, -2.5f}, 3.5f, 900, 11.0f), "drove east along the main street");
    CHECK(b.driveTo({11.0f, -4.5f}, 3.0f, 900, 10.0f), "reached the station entrance");
    b.shot("14_driving");
    CHECK(b.driveTo({14.5f, -14.0f}, 3.0f, 900, 8.0f), "entered the forecourt");
    CHECK(b.driveTo({17.0f, -17.5f}, 1.6f, 900, 6.0f), "reached the pump lane");
    b.stopCar();
    b.idle(10);
    b.shot("15_at_pump");
    CHECK(g.focusValid() && g.focus()->kind == IKind::FuelPump, "pump interaction available near the pump");
    // ---- refuel R$20
    int money0 = g.money();
    in = InputFrame();
    in.interactPressed = true;
    b.step(in, 1);
    b.idle(15);
    CHECK(g.panel().open, "fuel panel opened");
    b.shot("16_fuel_panel");
    g.selectPanelOptionPublic(0);
    b.idle(120);
    b.shot("17_after_fuel");
    CHECK(g.money() < money0, "money was spent on fuel");
    CHECK(car.fuel > fuel0 + 2.5f, "fuel was added to the tank");
    LOGI("fuel %.1f -> %.1f L, money %d -> %d", fuel0, car.fuel, money0, g.money());
    // ---- go to the market
    CHECK(b.driveTo({14.5f, -9.0f}, 3.0f, 900, 8.0f), "left the forecourt");
    CHECK(b.driveTo({20.0f, -2.0f}, 3.0f, 900, 9.0f), "back on the main street");
    CHECK(b.driveTo({22.0f, 2.5f}, 2.0f, 900, 8.0f), "parked near the market");
    b.stopCar();
    // exit car
    in = InputFrame();
    in.enterExitPressed = true;
    b.step(in, 1);
    b.idle(30);
    CHECK(g.player().vehicle < 0, "left the car");
    // walk to the door
    CHECK(b.walkTo({22.0f, 7.4f}, 0.7f, 400), "walked to the market door");
    b.idle(5);
    CHECK(g.focusValid() && g.focus()->kind == IKind::Door, "door interaction available");
    b.shot("20_market_door");
    in = InputFrame();
    in.interactPressed = true;
    b.step(in, 1);
    b.idle(60);
    CHECK(g.player().indoors, "entered the market interior");
    b.shot("21_market_interior_topdown");
    g.toggleCamera();
    b.idle(60);
    b.shot("22_market_interior_third");
    // talk to the clerk
    CHECK(b.walkTo({304.2f, -1.6f}, 0.8f, 500), "walked to the counter");
    b.idle(10);
    bool talk = false;
    for (auto& it : g.focusList()) if (it.kind == IKind::Npc) talk = true;
    CHECK(talk, "clerk can be talked to");
    LOGI("player at %.2f,%.2f nearby=%zu", g.player().pos.x, g.player().pos.y, g.focusList().size());
    // interact with whichever is the nearest focus; if not the clerk, step closer
    in = InputFrame();
    in.interactPressed = true;
    b.step(in, 1);
    b.idle(15);
    if (!g.panel().open) { b.walkTo({304.6f, -2.4f}, 0.4f, 100); in.interactPressed = true; b.step(in, 1); b.idle(15); }
    CHECK(g.panel().open, "dialogue panel opened");
    b.shot("23_attendant_dialog");
    g.selectPanelOptionPublic(0);   // see products
    b.idle(10);
    b.shot("24_shop_panel");
    int m1 = g.money();
    g.selectPanelOptionPublic(0);   // buy water
    b.idle(5);
    g.selectPanelOptionPublic(7);   // buy first aid kit
    b.idle(5);
    CHECK(g.money() < m1 && g.itemCount(8) >= 1, "bought items with real money");
    g.selectPanelOptionPublic(8);   // close
    b.idle(10);
    CHECK(!g.panel().open, "shop panel closed");
    // radial wheel
    g.toggleCamera();
    b.idle(40);
    in = InputFrame();
    in.wheelPressed = true; in.wheelHeld = true; in.wheelBtnHeld = true; in.wheelPos = {800.0f, 120.0f};
    b.step(in, 1);
    in.wheelPressed = false;
    b.step(in, 12);
    in.wheelPos = {1000.0f, 150.0f};
    b.step(in, 12);
    b.shot("25_wheel_open");
    CHECK(g.player().health > 0, "wheel opened without issues");
    in.wheelHeld = false; in.wheelBtnHeld = false; in.wheelReleased = true;
    b.step(in, 2);
    b.idle(30);
    // leave
    b.walkTo({305.8f, 2.8f}, 0.6f, 300);
    CHECK(b.walkTo({300.0f, 4.6f}, 0.8f, 600), "walked to the exit door");
    in = InputFrame();
    in.interactPressed = true;
    b.step(in, 1);
    b.idle(70);
    CHECK(!g.player().indoors, "left the market");
    // ---- back to the car and to the workshop
    b.walkTo(g.vehicles()[0].pos + Vec2{-1.6f, 0}, 1.0f, 400);
    in = InputFrame();
    in.enterExitPressed = true;
    b.step(in, 1);
    b.idle(30);
    CHECK(g.player().vehicle == 0, "re-entered the car");
    g.vehicles()[0].health = 64;   // some wear to repair
    CHECK(b.driveTo({-3.0f, 3.0f}, 3.5f, 900, 10.0f), "drove west to the junction");
    CHECK(b.driveTo({-26.0f, 3.0f}, 4.0f, 1200, 11.0f), "drove to the workshop street");
    CHECK(b.driveTo({-26.0f, 12.0f}, 3.0f, 900, 8.0f), "entered the workshop forecourt");
    CHECK(b.driveTo({-26.0f, 24.0f}, 1.8f, 900, 6.0f), "parked in the service bay");
    b.stopCar();
    b.idle(10);
    b.shot("30_workshop_bay");
    CHECK(g.focusValid() && g.focus()->kind == IKind::Workshop, "workshop interaction available");
    int m2 = g.money();
    in = InputFrame();
    in.interactPressed = true;
    b.step(in, 1);
    b.idle(15);
    CHECK(g.panel().open, "workshop panel opened");
    b.shot("31_workshop_panel");
    g.selectPanelOptionPublic(0);
    b.idle(10);
    CHECK(g.vehicles()[0].health >= 99.9f && g.money() < m2, "vehicle repaired and the service was paid");
    // ---- save and reload
    float savedFuel = g.vehicles()[0].fuel;
    int savedMoney = g.money();
    Vec2 savedPos = g.vehicles()[0].pos;
    CHECK(g.saveGame(), "game saved");
    b.shot("32_before_reload");
    // wipe state, then load
    g.money() = 0;
    g.vehicles()[0].fuel = 1.0f;
    g.vehicles()[0].pos = {0, 0};
    CHECK(g.loadGame(), "game loaded");
    CHECK(g.money() == savedMoney && std::fabs(g.vehicles()[0].fuel - savedFuel) < 0.01f && (g.vehicles()[0].pos - savedPos).length() < 0.01f, "state restored after reload");
    b.idle(20);
    b.shot("33_after_reload");
    LOGI("MVP scenario finished: %d failures, %d frames", g_failures, b.frames);
    return g_failures ? 10 : 0;
  }
  return 0;
}
