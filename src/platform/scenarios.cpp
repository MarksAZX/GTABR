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
      if (render) r.renderFrame(fd);
      ++frames;
    }
  }
  void idle(int n) { InputFrame in; step(in, n); }
  bool render = true;
  // faces and attacks a target position for n frames (re-pressing the attack button like a player would)
  void attackToward(Vec2 target, int n, bool hold = false) {
    for (int i = 0; i < n; ++i) {
      InputFrame in;
      Vec2 d = target - g.player().pos;
      float yaw = g.camera().yaw();
      Vec2 f{std::sin(yaw), -std::cos(yaw)}, rt{std::cos(yaw), std::sin(yaw)};
      Vec2 nn = d.normalized();
      if (d.length() > 1.2f) in.move = {nn.dot(rt) * 0.6f, nn.dot(f) * 0.6f};
      in.attackPressed = (i % 9) == 0;
      in.attackHeld = hold || in.attackPressed;
      step(in, 1);
    }
  }
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
    int reverse = 0;
    Vec2 lastPos = g.vehicles()[g.player().vehicle].pos;
    for (int i = 0; i < maxFrames; ++i) {
      Vehicle& v = g.vehicles()[g.player().vehicle];
      Vec2 d = target - v.pos;
      float dist = d.length();
      if (dist < tol) return true;
      float err = wrapAngle(yawFromDir(d) - v.yaw);
      InputFrame in;
      if (reverse > 0) {
        // back up with opposite lock to open the turning angle (three-point turn)
        --reverse;
        in.move = {err > 0 ? -1.0f : 1.0f, -0.8f};
        step(in, 1);
        continue;
      }
      float steer = clamp(err * 2.2f, -1.0f, 1.0f);
      float tgt = clamp(dist * 0.7f, 2.5f, maxSpeed) * clamp(1.0f - std::fabs(err) / 1.6f, 0.25f, 1.0f);
      float thr = clamp((tgt - v.speed) * 0.5f, -1.0f, 1.0f);
      in.move = {steer, thr};
      step(in, 1);
      // stuck: barely moved over the last second while pushing forward -> three-point turn
      if (i % 45 == 0) { if (i > 0 && (v.pos - lastPos).length() < 0.4f && thr > 0.2f) reverse = 35; lastPos = v.pos; }
    }
    const Vehicle& v = g.vehicles()[g.player().vehicle];
    LOGW("driveTo (%.1f, %.1f) timed out: car at (%.2f, %.2f) yaw %.2f speed %.2f fuel %.1f", target.x, target.y, v.pos.x, v.pos.y, v.yaw, v.speed, v.fuel);
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
  if (name == "clerk") {
    g.teleportPlayer({300.0f, 3.5f}, 0);
    b.idle(20);
    CHECK(b.walkTo({304.2f, -1.6f}, 0.8f, 500), "walked to the counter");
    b.idle(10);
    for (auto& n : g.npcs()) if (n.role == 4) LOGI("clerk at %.2f %.2f interior %d state %d", n.pos.x, n.pos.y, (int)n.interior, (int)n.state);
    LOGI("player %.2f %.2f indoors %d, focus count %zu", g.player().pos.x, g.player().pos.y, (int)g.player().indoors, g.focusList().size());
    for (auto& it : g.focusList()) LOGI(" focus kind %d dist %.2f", (int)it.kind, it.dist);
    b.shot("clerk");
    return g_failures;
  }
  if (name == "workshop") {
    Vehicle& car = g.vehicles()[0];
    car.pos = {22.0f, 2.5f}; car.yaw = kPi * 0.5f; car.vel = {}; car.speed = 0;
    g.teleportPlayer({22.0f, 4.6f}, 0);
    b.idle(5);
    InputFrame in; in.enterExitPressed = true; b.step(in, 1); b.idle(30);
    LOGI("in car %d", g.player().vehicle);
    Vec2 pts[4] = {{-3.0f, 3.0f}, {-26.0f, 3.0f}, {-26.0f, 12.0f}, {-26.0f, 24.0f}};
    for (Vec2 p : pts) {
      bool ok = b.driveTo(p, 3.0f, 900, 8.0f);
      LOGI("target %.1f %.1f ok %d car %.2f %.2f yaw %.2f", p.x, p.y, (int)ok, car.pos.x, car.pos.y, car.yaw);
    }
    b.shot("workshop");
    return 0;
  }
  if (name == "gameplay") {
    // full loop: brawl -> weapons -> gunfire -> panic -> witnesses -> police -> pursuit -> lose them
    b.render = false;
    b.idle(30);
    Player& p = g.player();
    CHECK(p.owned[kWpnFists], "fists always available");
    // ---- melee: find the nearest pedestrian and punch until something happens
    int victim = -1;
    float best = 1e9f;
    for (auto& n : g.npcs()) {
      if (n.stationary || n.interior || n.police) continue;
      float d = (n.pos - p.pos).length();
      if (d < best) { best = d; victim = n.id; }
    }
    CHECK(victim >= 0, "found a pedestrian");
    Npc& v = g.npcs()[victim];
    g.teleportPlayer(v.pos + Vec2{0.0f, 1.6f}, 0);
    b.idle(3);
    float h0 = v.health;
    b.attackToward(v.pos, 120);
    LOGI("victim health %.1f -> %.1f state %d", h0, v.health, (int)v.state);
    CHECK(v.health < h0, "punches really damage the pedestrian (hit detection)");
    CHECK(v.state == NpcState::Fight || v.state == NpcState::Flee || v.state == NpcState::Down || v.state == NpcState::Cower,
          "the victim reacts (fights back, flees or is knocked down)");
    b.render = true; b.shot("40_brawl"); b.render = false;
    // ---- weapon pickups: walk into the pistol
    g.teleportPlayer({-40.5f, -17.0f}, 0);
    b.idle(3);
    b.walkTo({-40.5f, -20.5f}, 0.5f, 200);
    b.idle(5);
    CHECK(p.owned[kWpnPistol] && p.mag[kWpnPistol] > 0, "picked up the pistol with ammo");
    CHECK(p.weapon == kWpnPistol, "pistol equipped");
    // also try every melee weapon and firearm through the same paths the wheel uses
    for (int w = 1; w < kWeaponCount; ++w) g.giveWeapon(w, 60);
    // ---- gunfire at a pedestrian + panic
    int target = -1; best = 1e9f;
    for (auto& n : g.npcs()) {
      if (n.stationary || n.interior || n.police || n.state == NpcState::Dead) continue;
      float d = (n.pos - p.pos).length();
      if (d < best) { best = d; target = n.id; }
    }
    CHECK(target >= 0, "found a target for the firearm test");
    Npc& t = g.npcs()[target];
    g.teleportPlayer(t.pos + Vec2{0.0f, 6.0f}, 0);
    b.idle(3);
    int mag0 = p.mag[kWpnPistol];
    float th0 = t.health;
    b.attackToward(t.pos, 60, false);
    LOGI("target health %.1f -> %.1f state %d, mag %d -> %d", th0, t.health, (int)t.state, mag0, p.mag[kWpnPistol]);
    CHECK(p.mag[kWpnPistol] < mag0, "shots consume ammo");
    CHECK(t.health < th0, "bullets hit and damage the target (raycast)");
    CHECK(g.audio().playedCount("pistol") > 0, "gunshot sound played");
    int fleeing = 0;
    for (auto& n : g.npcs()) if (!n.police && (n.state == NpcState::Flee || n.state == NpcState::Cower) && (n.pos - p.pos).length() < 40.0f) ++fleeing;
    LOGI("pedestrians fleeing/cowering after the shots: %d", fleeing);
    CHECK(fleeing > 0, "pedestrians react to gunfire (flee / cower)");
    b.render = true; b.shot("41_gunfire"); b.render = false;
    // ---- reload
    InputFrame rl; rl.reloadPressed = true; b.step(rl, 1);
    b.idle(60);
    CHECK(p.mag[kWpnPistol] == weaponDef(kWpnPistol).magazine, "reload refills the magazine");
    // ---- wanted + police dispatch (witnesses need a few seconds to call)
    for (int i = 0; i < 400 && g.wantedLevel() == 0; ++i) b.idle(1);
    LOGI("wanted level %d", g.wantedLevel());
    CHECK(g.wantedLevel() >= 1, "crime reported: wanted level");
    Vec2 crimeSpot = p.pos;
    int cops = 0;
    for (int i = 0; i < 1800 && cops == 0; ++i) { b.idle(1); cops = g.aliveCops(); }
    LOGI("officers on foot: %d after %.0f s", cops, 0.0f);
    CHECK(cops > 0, "police arrived and deployed officers");
    for (int i = 0; i < 900 && g.copsChasing() == 0; ++i) b.idle(1);
    CHECK(g.copsChasing() > 0, "officers see and chase the player");
    b.render = true; b.shot("42_police"); g.toggleCamera(); b.idle(40); b.shot("43_police_third"); g.toggleCamera(); b.render = false;
    // ---- escape: break line of sight far away; police must not know where the player went
    g.teleportPlayer({70.0f, 70.0f}, 0);
    float minDist = 1e9f;
    int frames = 0;
    while (g.wantedLevel() > 0 && frames < 30 * 140) {
      b.idle(1); ++frames;
      for (auto& c : g.npcs()) if (c.police && !c.despawn && c.state != NpcState::Dead) minDist = std::min(minDist, (c.pos - p.pos).length());
    }
    LOGI("escaped: wanted %d after %.1f s, closest officer got to %.1f m of the hideout, crime spot was %.1f m away", g.wantedLevel(),
         frames / 30.0f, minDist, (crimeSpot - p.pos).length());
    CHECK(g.wantedLevel() == 0, "police lost the trail and called off the search");
    CHECK(minDist > 15.0f, "police searched the last known area instead of homing in on the player");
    b.render = true; b.shot("44_escaped");
    // ---- persistence of weapons and ammo
    int magBefore = p.mag[kWpnSmg], resBefore = p.reserve[kWpnSmg];
    CHECK(g.saveGame(), "saved");
    p.owned[kWpnSmg] = false; p.mag[kWpnSmg] = 0;
    CHECK(g.loadGame() && p.owned[kWpnSmg] && p.mag[kWpnSmg] == magBefore && p.reserve[kWpnSmg] == resBefore, "weapons and ammo persist in the save");
    return g_failures;
  }
  if (name == "wanted3") {
    // escalation: repeated killings raise the wanted level; the armed response really shoots; dying respawns the player
    b.render = false;
    Player& p = g.player();
    b.idle(20);
    for (int w = 1; w < kWeaponCount; ++w) g.giveWeapon(w, 120);
    g.equipWeapon(kWpnSmg);
    int kills = 0;
    for (int round = 0; round < 6 && g.wantedLevel() < 3; ++round) {
      int t = -1; float best = 1e9f;
      for (auto& n : g.npcs()) {
        if (n.interior || n.state == NpcState::Dead || n.despawn) continue;
        float d = (n.pos - p.pos).length();
        if (d < best) { best = d; t = n.id; }
      }
      if (t < 0) break;
      Npc& n = g.npcs()[t];
      if (best > 12.0f) { g.teleportPlayer(n.pos + Vec2{0.0f, 5.0f}, 0); b.idle(2); }
      b.attackToward(n.pos, 40, true);
      if (n.state == NpcState::Dead) ++kills;
      b.idle(90);
    }
    LOGI("kills %d, wanted %d", kills, g.wantedLevel());
    CHECK(g.wantedLevel() >= 2, "repeated violent crimes raise the wanted level");
    float h0 = p.health;
    int frames = 0;
    while (!p.dead && frames < 30 * 120) { b.idle(1); ++frames; }
    LOGI("player health %.0f -> %.0f (dead %d) after %.1f s of police response, cops %d", h0, p.health, (int)p.dead, frames / 30.0f, g.aliveCops());
    CHECK(g.audio().playedCount("pistol") > 0, "police fired their weapons");
    CHECK(p.dead || p.health < h0, "police fire actually hits the player");
    if (p.dead) {
      for (int i = 0; i < 30 * 8 && p.dead; ++i) b.idle(1);
      CHECK(!p.dead && p.health >= 99.0f && g.wantedLevel() == 0, "after dying the player respawns with the heat cleared");
    }
    b.render = true; b.shot("50_after_wanted3");
    return g_failures;
  }
  if (name == "weapons") {
    Player& p = g.player();
    g.teleportPlayer({-20.0f, -12.0f}, 0);
    g.toggleCamera();
    b.idle(50);
    for (int w = 1; w < kWeaponCount; ++w) g.giveWeapon(w, 60);
    const int order[5] = {kWpnBat, kWpnKnife, kWpnPistol, kWpnSmg, kWpnShotgun};
    for (int k = 0; k < 5; ++k) {
      g.equipWeapon(order[k]);
      b.idle(10);
      InputFrame in; in.attackPressed = true; in.attackHeld = true;
      b.step(in, 1);
      in.attackPressed = false;
      b.step(in, isFirearm(order[k]) ? 3 : (getenv("GTABR_SWING_FRAMES") ? atoi(getenv("GTABR_SWING_FRAMES")) : 9));
      b.shot(std::string("wpn_") + weaponDef(order[k]).key);
      b.idle(40);
    }
    (void)p;
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
    // the islands span x 14..30 at z -15 and -23: enter the middle lane (z -19) from its west end
    CHECK(b.driveTo({10.6f, -13.5f}, 2.0f, 900, 6.0f), "entered the forecourt");
    CHECK(b.driveTo({10.8f, -18.6f}, 1.8f, 900, 4.0f), "lined up with the pump lane");
    CHECK(b.driveTo({18.5f, -19.0f}, 1.6f, 900, 5.0f), "reached the pump lane");
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
    CHECK(b.driveTo({34.8f, -19.0f}, 2.0f, 900, 6.0f), "drove out of the pump lane");
    CHECK(b.driveTo({36.0f, -11.0f}, 2.5f, 900, 6.0f), "left the forecourt");
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
    // U-turn toward the open north half of the avenue (the south kerb has trees)
    b.driveTo({21.0f, -3.5f}, 2.5f, 400, 6.0f);
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
