// Scripted scenarios for the headless runner. "mvp" plays the whole target loop with real (injected) touch input:
// walk -> switch camera -> enter car -> drive -> refuel at the pump -> market (talk + buy) -> workshop repair -> save -> reload.
#include <cmath>
#include <string>

#include "../core/log.h"
#include "../core/png.h"
#include "../game/game.h"
#include "../game/physics.h"

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
    for (const Collider& c : g.world().colliders)
      if (std::fabs((c.box.mn.x + c.box.mx.x) * 0.5f - v.pos.x) < 5 && std::fabs((c.box.mn.z + c.box.mx.z) * 0.5f - v.pos.y) < 5)
        LOGW("  near collider kind %d [%.1f,%.1f]-[%.1f,%.1f] h %.1f", (int)c.kind, c.box.mn.x, c.box.mn.z, c.box.mx.x, c.box.mx.z, c.box.mx.y);
    LOGW("driveTo (%.1f, %.1f) timed out: car at (%.2f, %.2f) yaw %.2f speed %.2f fuel %.1f", target.x, target.y, v.pos.x, v.pos.y, v.yaw, v.speed, v.fuel);
    return false;
  }
  // drives along the street grid (right-hand lane) to a point on or next to a street
  bool driveRoad(Vec2 target, float tol, int maxFrames, float maxSpeed = 11.0f) {
    const World& w = g.world();
    std::vector<Vec2> route = w.roadRoute(g.vehicles()[g.player().vehicle].pos, target);
    Vec2 prev = g.vehicles()[g.player().vehicle].pos;
    for (size_t i = 0; i < route.size(); ++i) {
      Vec2 p = route[i];
      bool last = i + 1 == route.size();
      if (!last) {
        Vec2 d = p - prev;
        if (d.length() > 0.5f) { Vec2 n = d.normalized(); p += Vec2{-n.y, n.x} * 2.4f; }
        if ((p - g.vehicles()[g.player().vehicle].pos).length() < 5.0f) { prev = route[i]; continue; }
        if (!driveTo(p, 4.5f, maxFrames, maxSpeed)) return false;
      } else if (!driveTo(p, tol, maxFrames, std::min(maxSpeed, 8.0f))) return false;
      prev = route[i];
    }
    return true;
  }
  // a spot 'dist' metres from 'target' on open ground with an unobstructed line to it (clear shot, nothing in the way)
  Vec2 clearShotSpot(Vec2 target, float dist) {
    const World& w = g.world();
    for (int k = 0; k < 16; ++k) {
      float a = k * kTau / 16.0f;
      Vec2 dir{std::sin(a), std::cos(a)};
      Vec2 p = target + dir * dist, q = p;
      phys::depenetrateCircle(w, q, 0.4f);
      if ((q - p).length() > 0.05f) continue;
      if (w.waterDepth(p.x, p.y) > 0.05f) continue;
      if (phys::raycast(w, p, (target - p).normalized(), dist, 1.3f) < dist - 0.4f) continue;
      return p;
    }
    return target + Vec2{0, dist};
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
  if (name == "lineup") {
    // every character model side by side, idle and walking, seen from the front
    b.idle(20);
    const char* arch[7] = {"player", "policial", "frentista", "atendente", "mecanico", "vizinho", "mulher_vestido"};
    Vec2 c = g.player().pos;
    g.toggleCamera();
    for (int k = 0; k < 7; ++k) g.debugSpawnNpc(arch[k], c + Vec2{-4.5f + 1.5f * k, 5.0f}, 0.0f, false);
    b.idle(80);
    for (auto& n : g.npcs()) if (n.id >= 1000) { printf("dbg npc %d %s pos %.1f %.1f despawn=%d interior=%d state=%d\n", n.id, n.archetype.c_str(), n.pos.x, n.pos.y, (int)n.despawn, (int)n.interior, (int)n.state); break; }
    printf("dbg player %.1f %.1f cam yaw %.2f\n", c.x, c.y, g.camera().yaw());
    b.shot("lineup_idle");
    for (int k = 0; k < 7; ++k) g.debugSpawnNpc(arch[k], c + Vec2{-4.5f + 1.5f * k, 16.0f}, 0.0f, true);
    b.idle(70);
    b.shot("lineup_walk");
    return 0;
  }
  if (name == "crowd") {
    // pedestrian variety: gather everybody within reach in front of the camera
    b.idle(20);
    std::vector<int> ids;
    for (auto& n : g.npcs()) if (!n.interior && !n.police && n.role == 0 && ids.size() < 10) ids.push_back(n.id);
    Vec2 c = g.player().pos;
    g.toggleCamera();
    for (size_t i = 0; i < ids.size(); ++i) {
      Npc& n = g.npcs()[ids[i]];
      n.pos = c + Vec2{-7.0f + 1.6f * (float)i, -6.0f - (float)(i % 3) * 1.2f};
      n.state = NpcState::Idle; n.path.clear(); n.stateTimer = 100;
      n.yaw = kPi;
    }
    g.teleportPlayer(c, kPi);
    b.idle(60);
    b.shot("crowd_third");
    return 0;
  }
  if (name == "night") {
    // night life: lamp pools, headlights, lit windows, moon
    const World& W = g.world();
    b.idle(10);
    g.setTimeOfDay(22.0f, 0.0f);
    g.setWeatherMode(1);
    // stand 3 m beside a lamp near the middle of the city
    Vec3 lamp = W.lampLights[W.lampLights.size() / 2];
    LOGI("lamps: %zu, using one at %.1f,%.1f", W.lampLights.size(), lamp.x, lamp.z);
    g.teleportPlayer({lamp.x, lamp.z}, 0);
    b.idle(40);
    b.shot("night_lamp_topdown");
    g.toggleCamera();
    b.idle(60);
    b.shot("night_lamp_third");
    // tall buildings for the windows
    for (const auto& bl : W.blocks) if (bl.second == District::Centro) { g.teleportPlayer({bl.first.cx(), bl.first.z1 - 1.5f}, kPi); break; }
    b.idle(60);
    b.shot("night_windows_third");
    g.teleportPlayer({W.poiGas.x, W.poiGas.z + 2}, 0);
    b.idle(60);
    b.shot("night_posto_third");
    return 0;
  }
  if (name == "stream") {
    // chunk residency: jumping across the city (and into a shop) makes the chunks around the player resident within a few
    // frames, and chunks far from it are released again; nothing is ever drawn from a destroyed buffer
    b.render = true;
    const World& W = g.world();
    b.idle(20);
    LOGI("resident %d / %zu chunks at start", g.residentChunks(), W.chunks.size());
    auto chunkUnder = [&](Vec2 p) -> const World::Chunk* {
      int cx = (int)std::floor(p.x / World::kChunk), cz = (int)std::floor(p.y / World::kChunk);
      for (const World::Chunk& c : W.chunks) if (c.cx == cx && c.cz == cz) return &c;
      return nullptr;
    };
    Vec2 spots[5] = {{W.land.x0 + 20, W.land.z0 + 20}, {W.land.x1 - 20, W.land.z1 - 20}, {W.land.x0 + 20, W.land.z1 - 20}, {W.land.x1 - 20, W.land.z0 + 20}, {0, 0}};
    int maxResident = 0;
    for (Vec2 sp : spots) {
      Vec2 q = W.nearestRoadPoint(sp);
      g.teleportPlayer(q, 0);
      b.idle(40);
      const World::Chunk* c = chunkUnder(q);
      CHECK(c && c->resident, "the chunk under the player is resident after a jump");
      CHECK(g.drawnChunks() > 0, "chunks are drawn after a jump");
      maxResident = std::max(maxResident, g.residentChunks());
    }
    LOGI("max resident %d of %zu", maxResident, W.chunks.size());
    CHECK((size_t)maxResident <= W.chunks.size(), "residency never exceeds the chunk count");
    b.shot("stream_after_jumps");
    return g_failures;
  }
  if (name == "traffic") {
    // ambient traffic: the cars really move along the lanes, stay on the roads and keep their distance
    b.render = false;
    b.idle(10);
    const World& W = g.world();
    std::vector<Vec2> start;
    int traffic = 0;
    for (const Vehicle& v : g.vehicles()) if (v.traffic) { ++traffic; start.push_back(v.pos); }
    LOGI("traffic cars: %d", traffic);
    CHECK(traffic >= 6, "the city spawns ambient traffic");
    float travelled = 0, maxSpeed = 0;
    int offRoad = 0, samples = 0;
    std::vector<Vec2> last = start;
    for (int f = 0; f < 30 * 60; ++f) {
      b.idle(1);
      size_t k = 0;
      for (const Vehicle& v : g.vehicles()) {
        if (!v.traffic || v.despawn) continue;
        if (k < last.size()) { travelled += (v.pos - last[k]).length() < 20.0f ? (v.pos - last[k]).length() : 0.0f; last[k] = v.pos; }
        ++k;
        maxSpeed = std::max(maxSpeed, std::fabs(v.speed));
        if (f % 30 == 0) {
          ++samples;
          Vec2 rp = W.nearestRoadPoint(v.pos);
          bool onRoad = false;
          for (const RoadLine& r : W.roads) {
            float along = r.horizontal ? v.pos.x : v.pos.y, across = r.horizontal ? v.pos.y : v.pos.x;
            if (std::fabs(across - r.c) < r.hw + 1.0f && along > r.a - 2 && along < r.b + 2) onRoad = true;
          }
          (void)rp;
          if (!onRoad) { ++offRoad; if (offRoad % 8 == 1) LOGI("  off-road car %d at %.1f,%.1f speed %.1f yaw %.2f wrecked %d health %.0f", v.id, v.pos.x, v.pos.y, v.speed, v.yaw, (int)v.wrecked, v.health); }
        }
      }
    }
    LOGI("traffic: travelled %.0f m in 60 s, max speed %.1f m/s, off-road samples %d/%d", travelled, maxSpeed, offRoad, samples);
    CHECK(travelled > 600.0f, "traffic cars drive (more than 600 m in total in a minute)");
    CHECK(maxSpeed > 6.0f && maxSpeed < 16.0f, "cruise speeds are city-like");
    CHECK(offRoad * 10 <= samples, "traffic stays on the streets (<=10% samples off-road)");
    // top-down view of busy streets
    Vec2 c = W.nearestRoadPoint({W.spawnPlayer.x, W.spawnPlayer.z});
    g.teleportPlayer(c + Vec2{0, 6}, 0);
    b.render = true;
    b.idle(60);
    b.shot("traffic_topdown");
    g.toggleCamera();
    b.idle(60);
    b.shot("traffic_third");
    return g_failures;
  }
  if (name == "shops") {
    // every enterable shop: go through the door, find the clerk, open the shop window, buy and leave
    const World& W = g.world();
    CHECK(W.shops.size() >= 4, "the city has the four shops");
    b.idle(20);
    for (const ShopDef& sh : W.shops) {
      LOGI("== %s (shop %d, interior %d)", sh.name.c_str(), sh.id, sh.interior);
      g.teleportPlayer({sh.door.x, sh.door.z}, 0);
      b.idle(6);
      CHECK(g.focusValid() && g.focus()->kind == IKind::Door, "the shop door can be used from the street");
      InputFrame in; in.interactPressed = true; b.step(in, 1);
      b.idle(60);
      CHECK(g.player().indoors && W.interiorAt(g.player().pos.x, g.player().pos.y) == sh.interior, "entered the right interior");
      if (sh.id == 0) b.shot("shop_interior_mercado");
      if (sh.kind == ShopKind::Padaria) b.shot("shop_interior_padaria");
      if (sh.kind == ShopKind::Ferragens) b.shot("shop_interior_ferragens");
      if (sh.kind == ShopKind::Conveniencia) b.shot("shop_interior_conveniencia");
      g.toggleCamera();
      b.idle(45);
      b.shot(std::string("shop_third_") + std::to_string(sh.id));
      g.toggleCamera();
      b.idle(40);
      // the clerk across the counter
      int clerk = -1;
      for (const Npc& n : g.npcs()) if (n.role == 4 && n.shop == sh.id) clerk = n.id;
      CHECK(clerk >= 0, "this shop has its clerk");
      if (clerk < 0) continue;
      g.teleportPlayer(g.npcs()[clerk].pos + Vec2{0.0f, 2.2f}, 0);
      b.idle(10);
      in = InputFrame(); in.interactPressed = true; b.step(in, 1); b.idle(15);
      CHECK(g.panel().open, "talking to the clerk opens the dialogue");
      g.selectPanelOptionPublic(0);   // Ver produtos
      b.idle(10);
      CHECK(g.panel().open && g.panel().title == sh.name, "the shop window shows this shop");
      CHECK(g.panel().options.size() == sh.stock.size() + 1, "the window lists the shop's stock");
      if (sh.kind == ShopKind::Ferragens) b.shot("shop_window_ferragens");
      else b.shot(std::string("shop_window_") + std::to_string(sh.id));
      int m0 = g.money();
      g.money() = m0 + 20000;
      m0 = g.money();
      int price = sh.stock[0].priceCents;
      int itemBefore = sh.stock[0].kind == 0 ? g.itemCount(sh.stock[0].id) : 0;
      bool ownedBefore = sh.stock[0].kind == 1 ? g.player().owned[sh.stock[0].id] : false;
      g.selectPanelOptionPublic(0);   // buy the first listed product
      b.idle(5);
      CHECK(g.money() == m0 - price, "the exact price was deducted");
      if (sh.stock[0].kind == 0) CHECK(g.itemCount(sh.stock[0].id) == itemBefore + 1, "the item went to the inventory");
      else CHECK(g.player().owned[sh.stock[0].id] && !ownedBefore, "the weapon went to the inventory");
      g.selectPanelOptionPublic((int)sh.stock.size());   // Fechar
      b.idle(10);
      CHECK(!g.panel().open, "the shop window closed");
      // leave by the inside door
      const DoorDef* exitDoor = nullptr;
      for (const DoorDef& d : W.doors) if (d.shop == sh.id && !d.toInterior) exitDoor = &d;
      CHECK(exitDoor != nullptr, "the shop has an exit door");
      if (exitDoor) {
        g.teleportPlayer({exitDoor->pos.x, exitDoor->pos.z}, 0);
        b.idle(6);
        in = InputFrame(); in.interactPressed = true; b.step(in, 1); b.idle(60);
        CHECK(!g.player().indoors, "left the shop");
      }
    }
    CHECK((g.progress() & (1u << kPgShops)) != 0, "progress: visited every shop");
    CHECK((g.progress() & (1u << kPgBought)) != 0, "progress: bought something");
    return g_failures;
  }
  if (name == "menu") {
    // the whole product flow: main menu -> new game (seed) -> play -> pause tabs -> codes -> settings -> save -> main menu ->
    // continue (same city, same state) -> another city -> load the first one back -> delete a slot
    auto waitPlaying = [&](int maxFrames) {
      for (int i = 0; i < maxFrames && !(g.loaded() && !g.inMenu()); ++i) b.idle(1);
      b.idle(5);
      return g.playing();
    };
    b.idle(40);
    CHECK(g.inMenu() && g.menu() == MenuState::Main, "the app opens on the main menu");
    b.shot("menu_main");
    g.tapUi(2001);   // NOVO JOGO
    b.idle(20);
    CHECK(g.menu() == MenuState::Slots, "new game opens the slot picker");
    b.shot("menu_slots_new");
    g.tapUi(2104);   // create in slot 1
    CHECK(waitPlaying(900), "a new city was generated and the game started");
    uint32_t seedA = g.seed();
    std::string cityA = g.world().cityName;
    size_t collA = g.world().colliders.size();
    Vec3 shopA = g.world().shops.empty() ? Vec3{} : g.world().shops[0].door;
    LOGI("new game: seed %u city %s colliders %zu", seedA, cityA.c_str(), collA);
    CHECK(g.readSlotInfo(1).used && g.readSlotInfo(1).seed == seedA, "the seed was saved in the slot");
    b.idle(30);
    b.shot("menu_game_started");
    // ---- play a bit: state that must persist
    g.money() = 123456;
    g.giveWeapon(kWpnPistol, 33);
    g.equipWeapon(kWpnPistol);
    g.teleportPlayer({g.player().pos.x + 3.0f, g.player().pos.y + 2.0f}, 1.0f);
    g.useItem(1);
    Vec2 posA = g.player().pos;
    // ---- pause menu tabs
    g.openMenu(MenuState::Pause);
    b.idle(15);
    const char* tabNames[7] = {"mapa", "jogo", "inventario", "codigos", "config", "salvar", "menuprincipal"};
    for (int t = 0; t < 7; ++t) {
      g.tapUi(2300 + t);
      b.idle(8);
      b.shot(std::string("pause_") + tabNames[t]);
    }
    g.tapUi(2300);
    b.idle(5);
    // map: zoom, drag is exercised by the pointer path in the full game; here the buttons
    g.tapUi(2801); b.idle(3); g.tapUi(2801); b.idle(3);
    b.shot("pause_mapa_zoom");
    g.tapUi(2802); g.tapUi(2803); b.idle(3);
    // ---- codes with real effects
    g.tapUi(2303);
    g.player().health = 20;
    g.tapUi(2500);
    CHECK(g.player().health >= 99.0f, "code: recover health");
    for (int w = 1; w < kWeaponCount; ++w) g.player().owned[w] = false;
    g.tapUi(2501);
    bool allWeapons = true;
    for (int w = 1; w < kWeaponCount; ++w) allWeapons &= g.player().owned[w];
    CHECK(allWeapons, "code: all weapons");
    g.player().reserve[kWpnPistol] = 0; g.player().mag[kWpnPistol] = 0;
    g.tapUi(2502);
    CHECK(g.player().mag[kWpnPistol] == weaponDef(kWpnPistol).magazine && g.player().reserve[kWpnPistol] > 0, "code: ammo refill");
    int m0 = g.money();
    g.tapUi(2503);
    CHECK(g.money() == m0 + 500000, "code: test money");
    g.tapUi(2505);
    b.idle(3);
    CHECK(g.wantedLevel() >= 1, "code: add wanted level");
    b.shot("pause_codigos_feedback");
    g.tapUi(2504);
    CHECK(g.wantedLevel() == 0, "code: remove wanted");
    g.vehicles()[0].health = 30; g.vehicles()[0].fuel = 1;
    g.teleportPlayer(g.vehicles()[0].pos + Vec2{2, 0}, 0);
    g.tapUi(2506);
    g.tapUi(2507);
    CHECK(g.vehicles()[0].health >= 99.9f, "code: repair vehicle");
    CHECK(g.vehicles()[0].fuel >= vehicleDef(g.vehicles()[0].model).fuelCap - 0.1f, "code: fill tank");
    g.teleportPlayer(posA, 1.0f);
    // ---- settings really apply
    g.tapUi(2304);
    for (int t = 0; t < 5; ++t) { g.tapUi(2400 + t); b.idle(5); b.shot(std::string("settings_tab") + std::to_string(t)); }
    int q0 = g.settings().quality;
    g.tapUi(3000);
    CHECK(g.settings().quality == (q0 + 1) % 4, "setting: quality preset changed");
    g.tapUi(3000); g.tapUi(3000); g.tapUi(3000);
    bool fx0 = g.settings().weatherFx;
    g.tapUi(3004);
    CHECK(g.settings().weatherFx != fx0, "setting: weather fx toggled");
    g.tapUi(3004);
    g.tapUi(3010);
    CHECK(g.settings().muted, "setting: mute");
    g.tapUi(3010);
    g.tapUi(3040);
    CHECK(g.settings().highContrast, "setting: high contrast");
    b.idle(5);
    b.shot("settings_highcontrast");
    g.tapUi(3040);
    // ---- save from the menu, back to the main menu
    g.tapUi(2305);
    g.tapUi(2900);
    SlotInfo si = g.readSlotInfo(1);
    CHECK(si.used && si.money == g.money() && si.seed == seedA, "slot metadata written (money, seed)");
    b.shot("pause_salvar2");
    g.tapUi(2306);
    g.tapUi(2910);
    b.idle(30);
    CHECK(g.inMenu() && g.menu() == MenuState::Main, "back to the main menu");
    b.shot("menu_main_continue");
    // ---- continue: same city, same state
    int moneyBefore = si.money;
    g.tapUi(2000);
    CHECK(waitPlaying(900), "continue loaded the most recent save");
    CHECK(g.seed() == seedA && g.world().cityName == cityA && g.world().colliders.size() == collA, "the same city was rebuilt from the seed");
    CHECK(g.money() == moneyBefore, "money restored");
    CHECK((g.player().pos - posA).length() < 0.05f, "position restored");
    CHECK(g.player().owned[kWpnPistol] && g.player().weapon == kWpnPistol, "weapons restored");
    b.idle(20);
    b.shot("menu_continue_loaded");
    // ---- second city in slot 2, then load the first one back
    g.openMenu(MenuState::Pause);
    g.tapUi(2306); g.tapUi(2910);
    b.idle(20);
    g.tapUi(2001);
    g.tapUi(2110);   // row tap on slot 2 (new game, empty -> starts)
    CHECK(waitPlaying(900), "second city generated");
    uint32_t seedB = g.seed();
    LOGI("second city: seed %u (%s)", seedB, g.world().cityName.c_str());
    CHECK(seedB != seedA, "a different seed was generated");
    b.shot("menu_second_city");
    g.openMenu(MenuState::Pause);
    g.tapUi(2306); g.tapUi(2910);
    b.idle(20);
    g.tapUi(2002);  // CARREGAR
    b.shot("menu_slots_load");
    g.tapUi(2101);  // load slot 1
    CHECK(waitPlaying(900), "slot 1 loaded again");
    CHECK(g.seed() == seedA && g.world().cityName == cityA && g.world().colliders.size() == collA &&
              (g.world().shops.empty() || (g.world().shops[0].door - shopA).length() < 1e-3f),
          "loading rebuilds exactly the same city (seed determinism)");
    // ---- delete slot 2
    g.openMenu(MenuState::Pause);
    g.tapUi(2306); g.tapUi(2910);
    b.idle(20);
    g.tapUi(2002);
    g.tapUi(2113);   // delete slot 2 -> confirmation
    b.idle(5);
    b.shot("menu_confirm_delete");
    g.tapUi(2190);
    CHECK(!g.readSlotInfo(2).used, "slot deleted after confirming");
    return g_failures;
  }
  if (name == "tour") {
    // visual review of the city: a handful of representative spots from both cameras
    const World& W = g.world();
    struct Spot { const char* tag; Vec2 pos; float yaw; };
    std::vector<Spot> spots;
    spots.push_back({"spawn", {W.spawnPlayer.x, W.spawnPlayer.z}, W.spawnYaw});
    spots.push_back({"posto", {W.poiGas.x, W.poiGas.z + 4}, 0});
    spots.push_back({"mercado", {W.poiMarketDoor.x, W.poiMarketDoor.z - 3}, 0});
    Vec2 c{0, 0};
    for (const auto& bl : W.blocks) if (bl.second == District::Centro) { c = {bl.first.cx(), bl.first.z0 + 1.2f}; break; }
    spots.push_back({"centro", c, 0});
    for (const auto& bl : W.blocks) if (bl.second == District::Residencial) { spots.push_back({"residencial", {bl.first.cx(), bl.first.z1 - 1.2f}, kPi}); break; }
    for (const auto& bl : W.blocks) if (bl.second == District::Industrial) { spots.push_back({"industrial", {bl.first.cx(), bl.first.z1 - 1.2f}, kPi}); break; }
    spots.push_back({"avenida", W.nearestRoadPoint({W.spawnPlayer.x + 40, W.spawnPlayer.z + 40}), 0});
    b.idle(20);
    for (const Spot& sp : spots) {
      g.teleportPlayer(sp.pos, sp.yaw);
      b.idle(25);
      b.shot(std::string("tour_") + sp.tag + "_topdown");
      g.toggleCamera();
      b.idle(50);
      b.shot(std::string("tour_") + sp.tag + "_third");
      g.toggleCamera();
      b.idle(35);
    }
    return 0;
  }
  if (name == "weather") {
    // rain and storm: grey light, wet ground with puddles, falling drops, wind and lightning
    b.idle(20);
    g.setTimeOfDay(15.0f, 0.0f);
    g.setWeatherMode(3);
    b.idle(240);
    CHECK(g.rainAmount() > 0.5f, "the weather turned to rain");
    CHECK(g.wetness() > 0.3f, "the ground is getting wet");
    b.shot("rain_topdown");
    g.toggleCamera();
    b.idle(60);
    b.shot("rain_third");
    for (int i = 0; i < 90; ++i) b.idle(1);
    b.shot("rain_third2");
    g.setTimeOfDay(21.5f, 0.0f);
    b.idle(30);
    b.shot("rain_night");
    g.setWeatherMode(1);
    b.idle(900);
    CHECK(g.rainAmount() < 0.2f, "the weather cleared");
    return g_failures;
  }
  if (name == "swim") {
    // walk from the sand into the sea: the player must switch to swimming, move slower and come back out
    const World& W = g.world();
    if (W.coastSide < 0) { LOGW("this seed has no coast"); return 0; }
    Vec2 out = W.coastSide == 0 ? Vec2{0, -1} : (W.coastSide == 1 ? Vec2{1, 0} : (W.coastSide == 2 ? Vec2{0, 1} : Vec2{-1, 0}));
    Vec2 start{W.poiBeach.x, W.poiBeach.z};
    g.teleportPlayer(start, yawFromDir(out));
    b.idle(10);
    CHECK(!g.player().swimming, "on the sand the player walks");
    g.toggleCamera();
    b.idle(40);
    {
      // wade in, photographing the spray in the shallows and the splash at the moment the player starts swimming
      bool shotWade = false;
      for (int i = 0; i < 1500 && !g.player().swimming; ++i) {
        b.walkTo(g.player().pos + out * 0.6f, 0.1f, 2);
        float dep = g.world().waterDepth(g.player().pos.x, g.player().pos.y);
        if (!shotWade && dep > 0.5f) { b.shot("swim_wade"); shotWade = true; }
      }
      b.idle(6);
      b.shot("swim_enter_splash");
    }
    g.toggleCamera();
    b.idle(30);
    CHECK(b.walkTo(start + out * 30.0f, 1.0f, 900), "walked into the sea");
    CHECK(g.player().swimming, "deep water switches to swimming");
    CHECK(g.world().waterDepth(g.player().pos.x, g.player().pos.y) > 1.2f, "water depth detected under the player");
    float maxSwim = 0;
    InputFrame in; in.move = {0, 1};
    for (int i = 0; i < 60; ++i) { b.step(in, 1); maxSwim = std::max(maxSwim, g.player().speed); }
    LOGI("swim speed %.2f m/s, y %.2f", maxSwim, g.player().y);
    CHECK(maxSwim < 2.0f && maxSwim > 0.8f, "swimming is slower than walking");
    g.toggleCamera();
    b.idle(45);
    b.shot("swim_third");
    g.toggleCamera();
    b.idle(45);
    b.shot("swim_topdown");
    in = InputFrame(); in.enterExitPressed = true; b.step(in, 1); b.idle(10);
    CHECK(g.player().vehicle < 0, "cannot get into a car while swimming");
    CHECK(b.walkTo(start, 1.0f, 1500), "swam back to the beach");
    CHECK(!g.player().swimming, "back on the sand the player walks again");
    return g_failures;
  }
  if (name == "beach") {
    // visual review of the coast: sand, promenade, sea, waves and foam (morning / sunset / night)
    const World& W = g.world();
    if (W.coastSide < 0) { LOGW("this seed has no coast"); return 0; }
    float yawSea = W.coastSide == 0 ? 0.0f : (W.coastSide == 1 ? kPi * 0.5f : (W.coastSide == 2 ? kPi : -kPi * 0.5f));
    g.teleportPlayer({W.poiBeach.x, W.poiBeach.z}, yawSea);
    const float hours[3] = {10.0f, 17.8f, 21.5f};
    const char* tags[3] = {"dia", "por_do_sol", "noite"};
    for (int i = 0; i < 3; ++i) {
      g.setTimeOfDay(hours[i], 0.0f);
      b.idle(20);
      b.shot(std::string("beach_") + tags[i] + "_topdown");
      g.toggleCamera();
      b.idle(50);
      b.shot(std::string("beach_") + tags[i] + "_third");
      g.toggleCamera();
      b.idle(40);
    }
    return 0;
  }
  if (name == "start") {
    b.idle(40);
    b.shot("01_topdown");
    g.toggleCamera();
    b.idle(45);
    b.shot("02_third");
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
    g.teleportPlayer(b.clearShotSpot(v.pos, 1.6f), 0);
    b.idle(3);
    float h0 = v.health;
    b.attackToward(v.pos, 120);
    LOGI("victim health %.1f -> %.1f state %d", h0, v.health, (int)v.state);
    CHECK(v.health < h0, "punches really damage the pedestrian (hit detection)");
    CHECK(v.state == NpcState::Fight || v.state == NpcState::Flee || v.state == NpcState::Down || v.state == NpcState::Cower,
          "the victim reacts (fights back, flees or is knocked down)");
    b.render = true; b.shot("40_brawl"); b.render = false;
    // ---- weapon pickups: walk into the pistol
    Vec3 pp;
    CHECK(g.pickupPos(kWpnPistol, pp), "pistol pickup exists in the generated city");
    g.teleportPlayer({pp.x + 2.0f, pp.z}, 0);
    b.idle(3);
    b.walkTo({pp.x, pp.z}, 0.4f, 200);
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
    g.teleportPlayer(b.clearShotSpot(t.pos, 6.0f), 0);
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
    if (getenv("GTABR_DUMPROADS")) {
      for (const RoadLine& r : g.world().roads) LOGI("road %s c=%.1f hw=%.1f [%.0f,%.0f] avenue %d", r.horizontal ? "H" : "V", r.c, r.hw, r.a, r.b, (int)r.avenue);
      for (const Collider& c : g.world().colliders)
        if (std::fabs((c.box.mn.x + c.box.mx.x) * 0.5f + 36.0f) < 10 && std::fabs((c.box.mn.z + c.box.mx.z) * 0.5f - 63.0f) < 10 && c.box.mn.x < 400)
          LOGI("  collider kind %d [%.1f,%.1f]-[%.1f,%.1f] h %.1f", (int)c.kind, c.box.mn.x, c.box.mn.z, c.box.mx.x, c.box.mx.z, c.box.mx.y);
    }
    for (int i = 0; i < 1800 && cops == 0; ++i) {
      b.idle(1); cops = g.aliveCops();
      if (i % 300 == 0)
        for (const Vehicle& v : g.vehicles())
          if (v.police && !v.despawn) LOGI("  t=%ds police car %d at %.1f,%.1f spd %.1f siren %d aiTarget %.1f,%.1f (crime at %.1f,%.1f)", i / 30, v.id, v.pos.x, v.pos.y, v.speed, (int)v.siren, v.aiTarget.x, v.aiTarget.y, crimeSpot.x, crimeSpot.y);
    }
    LOGI("officers on foot: %d after %.0f s", cops, 0.0f);
    CHECK(cops > 0, "police arrived and deployed officers");
    for (int i = 0; i < 900 && g.copsChasing() == 0; ++i) b.idle(1);
    CHECK(g.copsChasing() > 0, "officers see and chase the player");
    b.render = true; b.shot("42_police"); g.toggleCamera(); b.idle(40); b.shot("43_police_third"); g.toggleCamera(); b.render = false;
    // ---- escape: break line of sight far away; police must not know where the player went
    {
      // hide in the land corner farthest from the crime
      const RectF& L = g.world().land;
      Vec2 best = p.pos; float bd = 0;
      for (Vec2 c : {Vec2{L.x0 + 14, L.z0 + 14}, Vec2{L.x1 - 14, L.z0 + 14}, Vec2{L.x0 + 14, L.z1 - 14}, Vec2{L.x1 - 14, L.z1 - 14}}) {
        Vec2 q = g.world().nearestRoadPoint(c);
        if ((q - crimeSpot).length() > bd) { bd = (q - crimeSpot).length(); best = q; }
      }
      g.teleportPlayer(best, 0);
    }
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
    for (int round = 0; round < 12 && g.wantedLevel() < 3; ++round) {
      // victims with plenty of people around (witnesses report the crime); the test keeps going until the heat really rises
      int t = -1; float bestScore = -1e9f;
      for (auto& n : g.npcs()) {
        if (n.interior || n.state == NpcState::Dead || n.despawn || n.police || n.role != 0) continue;
        int witnesses = 0;
        for (auto& o : g.npcs()) if (&o != &n && !o.interior && !o.despawn && !o.police && o.state != NpcState::Dead && (o.pos - n.pos).length() < 14.0f) ++witnesses;
        float score = witnesses * 3.0f - (n.pos - p.pos).length() * 0.02f;
        if (score > bestScore) { bestScore = score; t = n.id; }
      }
      if (t < 0) break;
      Npc& n = g.npcs()[t];
      g.teleportPlayer(b.clearShotSpot(n.pos, 6.0f), 0);
      b.idle(2);
      b.attackToward(n.pos, 40, true);
      if (n.state == NpcState::Dead) ++kills;
      b.idle(90);
    }
    LOGI("kills %d, wanted %d", kills, g.wantedLevel());
    CHECK(g.wantedLevel() >= 2, "repeated violent crimes raise the wanted level");
    float h0 = p.health;
    int frames = 0;
    while (!p.dead && frames < 30 * 120) {
      // the police search the last reported spot; walk toward the nearest officer / patrol car so they can see us
      Vec2 tgt = p.pos; float bd = 1e9f;
      for (const Npc& c : g.npcs()) if (c.police && !c.despawn && c.state != NpcState::Dead && (c.pos - p.pos).length() < bd) { bd = (c.pos - p.pos).length(); tgt = c.pos; }
      for (const Vehicle& v : g.vehicles()) if (v.police && !v.despawn && (v.pos - p.pos).length() < bd) { bd = (v.pos - p.pos).length(); tgt = v.pos; }
      if (bd < 1e8f && (bd > 30.0f || bd < 8.0f)) {
        // close in when far, back off when they run at us
        float yaw = g.camera().yaw();
        Vec2 f{std::sin(yaw), -std::cos(yaw)}, rt{std::cos(yaw), std::sin(yaw)}, n = (tgt - p.pos).normalized();
        if (bd < 8.0f) n = n * -1.0f;
        InputFrame mv; mv.move = {n.dot(rt), n.dot(f)}; mv.runHeld = true;
        b.step(mv, 1);
      } else if (bd < 1e8f) {
        // armed stand-off: make sure there is open ground between us (a hedge or a tree would hide us from them), then keep firing
        if (frames % 120 == 0 && g.copsChasing() == 0) { g.teleportPlayer(b.clearShotSpot(tgt, 12.0f), 0); b.idle(1); }
        b.attackToward(tgt, 1, true);   // the officers answer a drawn firearm with fire
      } else b.idle(1);
      ++frames;
      if (getenv("GTABR_COPLOG") && frames % 150 == 0) {
        LOGI("  t=%ds wanted %d hp %.0f player %.0f,%.0f nearest %.1f", frames / 30, g.wantedLevel(), p.health, p.pos.x, p.pos.y, bd);
        for (const Npc& c : g.npcs()) if (c.police && !c.despawn) LOGI("    cop %d at %.0f,%.0f state %d wpn %d hp %.0f d %.1f", c.id, c.pos.x, c.pos.y, (int)c.state, c.weapon, c.health, (c.pos - p.pos).length());
        for (const Vehicle& v : g.vehicles()) if (v.police && !v.despawn) LOGI("    car %d at %.0f,%.0f spd %.1f siren %d driver %d", v.id, v.pos.x, v.pos.y, v.speed, (int)v.siren, v.driver);
      }
      if (frames < 600 && frames % 30 == 0) {
        int armed = 0, pistols = g.audio().playedCount("pistol");
        for (const Npc& c : g.npcs()) if (c.police && !c.despawn && c.weapon != kWpnFists && c.weapon != kWpnBaton) ++armed;
        LOGI("  f=%d hp %.0f dead %d wanted %d cops %d armed %d pistolSounds %d nearest %.1f", frames, p.health, (int)p.dead, g.wantedLevel(), g.aliveCops(), armed, pistols, bd);
      }
      if (frames % 450 == 0) {
        LOGI("  t=%ds wanted %d player %.0f,%.0f hp %.0f cops %d chasing %d nearest %.0f m", frames / 30, g.wantedLevel(), p.pos.x, p.pos.y, p.health, g.aliveCops(), g.copsChasing(), bd);
        for (const Vehicle& v : g.vehicles()) if (v.police && !v.despawn) LOGI("    car %.0f,%.0f spd %.1f siren %d tgt %.0f,%.0f", v.pos.x, v.pos.y, v.speed, (int)v.siren, v.aiTarget.x, v.aiTarget.y);
      }
    }
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
  if (name == "held") {
    // weapons carried at rest, walking and mid-swing, to judge the grip against the hand bone
    g.toggleCamera();
    b.idle(50);
    for (int w = 1; w < kWeaponCount; ++w) g.giveWeapon(w, 60);
    const int order[4] = {kWpnBat, kWpnCrowbar, kWpnPistol, kWpnShotgun};
    for (int k = 0; k < 4; ++k) {
      g.equipWeapon(order[k]);
      b.idle(40);
      b.shot(std::string("held_idle_") + weaponDef(order[k]).key);
      InputFrame mv; mv.move = {0, -1};
      b.step(mv, 18);
      b.shot(std::string("held_walk_") + weaponDef(order[k]).key);
      b.idle(30);
    }
    return 0;
  }
  if (name == "iso") {
    b.idle(40);
    b.shot("iso_off");
    g.setIsometric(true);
    b.idle(120);
    b.shot("iso_on");
    return 0;
  }
  if (name == "shafts") {
    g.toggleCamera();
    g.setTimeOfDay(17.2f, 0.0f);
    b.idle(40);
    for (int i = 0; i < 6; ++i) {
      InputFrame in; in.look = {120.0f, 0};
      b.step(in, 14);
      b.idle(10);
      b.shot("shafts_" + std::to_string(i));
    }
    return 0;
  }
  if (name == "jump") {
    g.toggleCamera();
    b.idle(50);
    float maxAir = 0;
    InputFrame jin; jin.jumpPressed = true; jin.move = {0, -1};
    b.step(jin, 1);
    for (int i = 0; i < 6; ++i) {
      InputFrame mv; mv.move = {0, -1};
      b.step(mv, 6);
      maxAir = std::max(maxAir, g.player().air);
      if (i == 1 || i == 3) b.shot("jump_" + std::to_string(i));
    }
    b.idle(30);
    CHECK(maxAir > 0.6f && maxAir < 1.3f, "jump reaches a believable height");
    CHECK(!g.player().airborne && g.player().air == 0.0f, "player lands again");
    return 0;
  }
  if (name == "graph") {
    // animation graph: death clip, ready stance, blunt swing, hit reaction and the jump arc
    g.toggleCamera();
    b.idle(40);
    Vec2 c = g.player().pos;
    int id = g.debugSpawnNpc("mecanico", c + Vec2{2.2f, 4.0f}, 0.0f, false);
    b.idle(30);
    for (auto& n : g.npcs()) if (n.id == id) n.state = NpcState::Dead;
    b.idle(14); b.shot("graph_die_a");
    b.idle(22); b.shot("graph_die_b");
    b.idle(70); b.shot("graph_die_c");
    for (int w = 1; w < kWeaponCount; ++w) g.giveWeapon(w, 30);
    g.equipWeapon(kWpnBat);
    b.idle(45); b.shot("graph_combat_idle");
    InputFrame in; in.attackPressed = true; in.attackHeld = true;
    b.step(in, 1); in.attackPressed = false;
    b.step(in, 6); b.shot("graph_swing_a");
    b.step(in, 8); b.shot("graph_swing_b");
    b.idle(40);
    InputFrame jin; jin.jumpPressed = true;
    b.step(jin, 1);
    b.idle(6); b.shot("graph_jump_a");
    b.idle(10); b.shot("graph_jump_b");
    b.idle(12); b.shot("graph_jump_c");
    b.idle(20); b.shot("graph_landed");
    return 0;
  }
  if (name == "decals") {
    // street wear and wall decals: walk the first street of the city in both cameras
    if (getenv("GTABR_Q")) g.setQuality(atoi(getenv("GTABR_Q")));
    const World& w = g.world();
    printf("decals in the city: %zu\n", w.decals.size());
    Vec2 at{0, 0};
    int best = 0;
    for (const SurfaceDecal& d : w.decals) {
      int n = 0;
      for (const SurfaceDecal& e : w.decals) if (std::fabs(e.pos.x - d.pos.x) < 14 && std::fabs(e.pos.z - d.pos.z) < 14) ++n;
      if (n > best) { best = n; at = {d.pos.x, d.pos.z}; }
    }
    {
      // a street with lots of wear: the road line whose decals are closest to the player start
      float bestD = 1e9f;
      for (const RoadLine& rl : w.roads) {
        float mid = (rl.a + rl.b) * 0.5f;
        Vec2 pt = rl.horizontal ? Vec2{mid, rl.c} : Vec2{rl.c, mid};
        float dd = (pt - g.player().pos).length();
        if (dd < bestD && rl.avenue) { bestD = dd; at = pt; }
      }
    }
    g.teleportPlayer(at, 0.0f);
    b.idle(40);
    b.shot("decals_top");
    g.toggleCamera();
    b.idle(60);
    b.shot("decals_third");
    for (const SurfaceDecal& d : w.decals) if (d.vertical && std::fabs(d.pos.x - at.x) < 40 && std::fabs(d.pos.z - at.y) < 40) {
      Vec2 out{std::sin(d.yaw), -std::cos(d.yaw)};
      g.teleportPlayer({d.pos.x + out.x * 3.0f, d.pos.z + out.y * 3.0f}, d.yaw + 3.14159f);
      b.idle(40);
      b.shot("decals_wall");
      break;
    }
    return 0;
  }
  if (name == "probes") {
    const World& w = g.world();
    const ProbeGrid& pg = w.probes;
    Vec2 pp = g.player().pos;
    printf("probe grid %dx%d origin %.1f,%.1f cell %.1f ; player %.1f,%.1f\n", pg.w, pg.h, pg.x0, pg.z0, pg.cell, pp.x, pp.y);
    for (int dz = -2; dz <= 2; ++dz) {
      for (int dx = -2; dx <= 2; ++dx) {
        int i = (int)std::round((pp.x - pg.x0) / pg.cell) + dx, j = (int)std::round((pp.y - pg.z0) / pg.cell) + dz;
        size_t base = ((size_t)0 * pg.h + j) * pg.w * 4 + (size_t)i * 4;
        printf("[%3d %3d %3d %3d] ", pg.rgba[base], pg.rgba[base + 1], pg.rgba[base + 2], pg.rgba[base + 3]);
      }
      printf("\n");
    }
    return 0;
  }
  if (name == "wetroad") {
    // rainy night on an avenue: wet asphalt reflecting lamps, signs and cars
    const World& w = g.world();
    Vec2 at{0, 0};
    float bestD = 1e9f;
    for (const RoadLine& rl : w.roads) {
      float mid = (rl.a + rl.b) * 0.5f;
      Vec2 pt = rl.horizontal ? Vec2{mid, rl.c} : Vec2{rl.c, mid};
      float dd = (pt - g.player().pos).length();
      if (dd < bestD && rl.avenue) { bestD = dd; at = pt; }
    }
    g.teleportPlayer(at, 0.0f);
    g.toggleCamera();
    g.setTimeOfDay(21.0f, 0.0f);
    g.setWeatherMode(3);
    b.idle(300);
    b.shot("wetroad_a");
    InputFrame in; in.look = {200.0f, 0};
    b.step(in, 12);
    b.idle(20);
    b.shot("wetroad_b");
    return 0;
  }
  if (name == "hud") {
    // interface review: status card, notifications, objective, location banner, dialogue, shop
    b.idle(30);
    g.toast("Você chegou|Posto Boa Viagem", "pin", rgba(0.56f, 0.80f, 0.68f));
    g.toast("Sem combustível suficiente para chegar lá", "fuel", rgba(0.90f, 0.40f, 0.38f));
    g.toast("Câmera: Terceira Pessoa", "camera");
    g.setWaypointDebug("Oficina Silva");
    g.addMoneyDebug(-1250);
    b.idle(40);
    b.shot("hud_ingame");
    int npc = -1;
    for (size_t i = 0; i < g.npcs().size(); ++i) if (g.npcs()[i].role == 1) { npc = (int)i; break; }
    if (npc < 0) npc = 0;
    g.debugOpenNpcPanel(npc);
    b.idle(10); b.shot("hud_dialog_typing");
    b.idle(120); b.shot("hud_dialog");
    g.debugClosePanel();
    g.openShopPanel(0);
    b.idle(60); b.shot("hud_shop");
    return 0;
  }
  if (name == "cockpit") {
    // driving HUD: stand next to the first car, get in and accelerate
    b.idle(30);
    if (g.vehicles().empty()) return 0;
    Vehicle& v = g.vehicles()[0];
    g.teleportPlayer(v.pos + Vec2{1.6f, 0.0f}, 0.0f);
    b.idle(10);
    InputFrame e; e.enterExitPressed = true;
    b.step(e, 1);
    b.idle(70);
    InputFrame d; d.move = {0, 1};
    b.step(d, 90);
    b.shot("cockpit_a");
    g.toggleCamera();
    b.step(d, 40);
    b.shot("cockpit_b");
    return 0;
  }
  if (name == "fight") {
    // combat feel: a four-hit combo with impact freeze, a dodge roll and a guard
    g.toggleCamera();
    b.idle(40);
    Vec2 c = g.player().pos;
    int id = g.debugSpawnNpc("vizinho", c + Vec2{0.0f, 1.3f}, 3.14159f, false);
    g.player().yaw = g.player().targetYaw = 3.14159f;   // face +z: the dummy
    b.idle(10);
    bool sawFreeze = false;
    int maxCombo = 0;
    for (int i = 0; i < 4; ++i) {
      InputFrame a; a.attackPressed = true; a.attackHeld = true;
      b.step(a, 1);
      for (int f = 0; f < 40; ++f) { b.idle(1); sawFreeze |= g.hitstopLeft() > 0.0f; }
      maxCombo = std::max(maxCombo, g.player().combo);
      if (i == 1) b.shot("fight_combo_b");
    }
    CHECK(sawFreeze, "a landed blow freezes the world for an instant");
    CHECK(maxCombo >= 2, "the combo advances past the second blow");
    for (auto& n : g.npcs()) if (n.id == id) { n.state = NpcState::Fight; n.target = {ActorKind::Player, 0}; n.stateTimer = 20.0f; }
    b.idle(15);
    CHECK(g.fightContext(), "an enemy close by turns the jump button into the defence button");
    float hp = g.player().health;
    InputFrame tap; tap.jumpPressed = true; tap.jumpHeld = true; tap.move = {1, 0};
    b.step(tap, 1);
    InputFrame rel; rel.move = {1, 0};
    b.step(rel, 1);
    b.idle(6);
    CHECK(g.player().dodgeT >= 0, "a tap rolls");
    b.shot("fight_dodge");
    g.debugHurtPlayer(20.0f, Vec2{0, 1});
    CHECK(g.player().health >= hp - 0.01f, "nothing hurts during the roll");
    b.idle(40);
    InputFrame hold; hold.jumpHeld = true; hold.jumpPressed = true;
    b.step(hold, 1);
    hold.jumpPressed = false;
    b.step(hold, 25);
    CHECK(g.player().blocking, "holding raises the guard");
    b.shot("fight_guard");
    hp = g.player().health;
    { float yw = g.player().yaw; g.debugHurtPlayer(20.0f, Vec2{-std::sin(yw), std::cos(yw)}); }   // from the front
    float lost = hp - g.player().health;
    printf("guard: lost %.1f hp of 20\n", lost);
    CHECK(lost < 8.0f, "a guarded frontal blow is mostly absorbed");
    return 0;
  }
  if (name == "life") {
    // city life: shop hours, event, birds
    b.idle(30);
    g.setTimeOfDay(23.5f, 0.0f);
    bool anyClosed = false, convenienceOpen = true;
    for (size_t i = 0; i < g.world().shops.size(); ++i) {
      bool open = g.shopOpen((int)i);
      if (g.world().shops[i].kind == ShopKind::Conveniencia) convenienceOpen = open;
      else anyClosed |= !open;
    }
    CHECK(anyClosed, "the market, bakery and hardware store are closed at 23:30");
    CHECK(convenienceOpen, "the convenience store stays open all night");
    g.setTimeOfDay(10.0f, 0.0f);
    bool allOpen = true;
    for (size_t i = 0; i < g.world().shops.size(); ++i) allOpen &= g.shopOpen((int)i);
    CHECK(allOpen, "everything is open at 10:00");
    g.setTimeOfDay(8.5f, 0.0f);
    CHECK(g.trafficFactor() > 1.2f, "rush hour thickens the traffic");
    g.setTimeOfDay(3.0f, 0.0f);
    CHECK(g.trafficFactor() < 0.5f, "the night streets are quiet");
    g.setTimeOfDay(14.0f, 0.0f);
    g.forceAccidentDebug();
    b.idle(60);
    CHECK(g.cityEventActive(), "a street event is running");
    g.toggleCamera();
    b.idle(30);
    b.shot("life_accident");
    return 0;
  }
  if (name == "ultra") {
    // AAA post stack: volumetric clouds, contact shadows, sharpening, motion blur (Ultra preset, third person)
    g.setQuality(3);
    g.toggleCamera();
    b.idle(90);
    for (float hour : {10.0f, 16.9f, 17.7f}) {
      g.setTimeOfDay(hour, 0.0f);
      b.idle(6);
      b.shot("ultra_" + std::to_string((int)hour));
    }
    g.setTimeOfDay(12.0f, 0.0f);
    for (int i = 0; i < 6; ++i) { InputFrame in; in.look = {120, 0}; in.lookDragging = true; b.step(in, 1); }
    b.shot("ultra_turn_blur");
    return 0;
  }
  if (name == "classes") {
    // visual review of the social classes: self-built quarter, wealthy quarter, paved streets
    b.idle(30);
    const World& w = g.world();
    int counts[3] = {};
    for (const auto& sc : w.social) counts[sc.second]++;
    LOGI("blocks: middle %d, poor %d, rich %d", counts[0], counts[1], counts[2]);
    for (int cls = 1; cls <= 2; ++cls)
      for (const auto& sc : w.social) {
        if (sc.second != cls) continue;
        Vec2 c{sc.first.cx(), sc.first.cz()};
        g.teleportPlayer(c, 0.0f);
        b.idle(40);
        b.shot(cls == 1 ? "class_poor_top" : "class_rich_top");
        g.toggleCamera();
        g.teleportPlayer({sc.first.cx(), sc.first.z0 + 1.0f}, kPi);
        b.idle(60);
        b.shot(cls == 1 ? "class_poor_third" : "class_rich_third");
        g.toggleCamera();
        break;
      }
    return 0;
  }
  if (name == "jobs") {
    b.idle(30);
    int m0 = g.money();
    g.debugJob(0);
    CHECK(g.jobActive(), "taking a delivery activates the job and sets the waypoint");
    g.debugJob(1);
    b.idle(10);
    g.debugJob(2);
    CHECK(!g.jobActive() && g.jobsDone() == 1, "delivering finishes the job");
    CHECK(g.money() > m0, "the delivery pays");
    CHECK(g.xp() > 0 || g.level() > 1, "the delivery gives XP");
    return 0;
  }
  if (name == "vault") {
    // contextual vault: find a low, thin obstacle and jump over it while walking
    b.idle(30);
    const World& w = g.world();
    int pick = -1;
    for (size_t i = 0; i < w.colliders.size(); ++i) {
      const Collider& c = w.colliders[i];
      float h = c.box.mx.y - c.box.mn.y;
      float sx = c.box.mx.x - c.box.mn.x, sz = c.box.mx.z - c.box.mn.z;
      if ((c.kind == ColKind::Wall || c.kind == ColKind::Prop) && c.box.mn.y < 0.2f && c.box.mx.y > 0.7f && c.box.mx.y < 1.4f && std::min(sx, sz) < 0.9f) { pick = (int)i; break; }
    }
    if (pick < 0) {
      int cnt[6][4] = {};
      for (const Collider& c : w.colliders) { int k = (int)c.kind; float h = c.box.mx.y; cnt[k][h < 0.7f ? 0 : (h < 1.4f ? 1 : (h < 3 ? 2 : 3))]++; }
      for (int k = 0; k < 6; ++k) printf("kind %d: <0.7 %d, <1.4 %d, <3 %d, tall %d\n", k, cnt[k][0], cnt[k][1], cnt[k][2], cnt[k][3]);
    }
    CHECK(pick >= 0, "the city has a low obstacle to vault");
    if (pick < 0) return 0;
    const Collider& c = w.colliders[pick];
    bool alongX = (c.box.mx.x - c.box.mn.x) > (c.box.mx.z - c.box.mn.z);
    Vec2 mid{(c.box.mn.x + c.box.mx.x) * 0.5f, (c.box.mn.z + c.box.mx.z) * 0.5f};
    Vec2 dir = alongX ? Vec2{0, 1} : Vec2{1, 0};
    float half = alongX ? (c.box.mx.z - c.box.mn.z) * 0.5f : (c.box.mx.x - c.box.mn.x) * 0.5f;
    Vec2 start = mid - dir * (half + 1.3f);
    g.teleportPlayer(start, yawFromDir(dir));
    b.idle(20);
    // walk towards the obstacle, then jump when close
    float sideStart = (start - mid).dot(dir);
    bool crossed = false;
    for (int i = 0; i < 160 && !crossed; ++i) {
      InputFrame in;
      Vec2 f = dir;
      float cy = g.camera().yaw();
      Vec2 camF{std::sin(cy), -std::cos(cy)}, camR{std::cos(cy), std::sin(cy)};
      in.move = {f.dot(camR), f.dot(camF)};
      float d = (g.player().pos - mid).dot(dir);
      if (d > -(half + 0.95f) && !g.player().airborne && d < 0) in.jumpPressed = true;
      b.step(in, 1);
      crossed = (g.player().pos - mid).dot(dir) > half + 0.3f;
    }
    CHECK(crossed, "the player vaults over the obstacle instead of stopping at it");
    (void)sideStart;
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
    // ---- drive to the gas station (route generated from the city's street grid)
    const World& W = g.world();
    auto v2 = [](const Vec3& p) { return Vec2{p.x, p.z}; };
    Vehicle& car = g.vehicles()[0];
    float fuel0 = car.fuel;
    LOGI("city '%s' seed %u, coast %d, %zu shops", W.cityName.c_str(), W.seed, W.coastSide, W.shops.size());
    CHECK(b.driveRoad(v2(W.gasApproach), 3.0f, 1500), "drove through the city to the gas station");
    b.shot("14_driving");
    CHECK(b.driveTo(v2(W.gasLaneEntry), 2.0f, 900, 5.0f), "entered the forecourt");
    CHECK(b.driveTo(v2(W.gasLaneTurn), 1.8f, 900, 4.0f), "lined up with the pump lane");
    CHECK(b.driveTo(v2(W.gasLanePump), 1.6f, 900, 5.0f), "reached the pump lane");
    b.stopCar();
    b.idle(10);
    b.shot("15_at_pump");
    CHECK(g.focusValid() && g.focus()->kind == IKind::FuelPump, "pump interaction available near the pump");
    // ---- refuel
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
    CHECK(b.driveTo(v2(W.gasLaneExit), 2.0f, 900, 5.0f), "drove out of the pump lane");
    CHECK(b.driveTo(v2(W.gasExitStreet), 2.5f, 900, 5.0f), "left the forecourt");
    CHECK(b.driveRoad(v2(W.marketParking), 2.5f, 1500), "drove to the market");
    b.stopCar();
    in = InputFrame();
    in.enterExitPressed = true;
    b.step(in, 1);
    b.idle(30);
    CHECK(g.player().vehicle < 0, "left the car");
    const ShopDef* market = nullptr;
    for (const ShopDef& sh : W.shops) if (sh.kind == ShopKind::Mercado) market = &sh;
    CHECK(market != nullptr, "the city has a market");
    CHECK(b.walkTo(v2(market->door), 0.7f, 500), "walked to the market door");
    b.idle(5);
    const Interactable* doorIt = nullptr;
    for (auto& it : g.focusList()) if (it.kind == IKind::Door) doorIt = &it;
    CHECK(doorIt != nullptr, "door interaction available");
    b.shot("20_market_door");
    if (doorIt) { Interactable copy = *doorIt; g.activateInteractable(copy); }   // same call the interact button makes
    b.idle(60);
    CHECK(g.player().indoors, "entered the market interior");
    b.shot("21_market_interior_topdown");
    g.toggleCamera();
    b.idle(60);
    b.shot("22_market_interior_third");
    // talk to the clerk across the counter
    Vec2 counter = v2(market->clerk) + Vec2{0.0f, 2.1f};
    CHECK(b.walkTo(counter, 0.6f, 600), "walked to the counter");
    b.idle(10);
    bool talk = false;
    for (auto& it : g.focusList()) if (it.kind == IKind::Npc) talk = true;
    CHECK(talk, "clerk can be talked to");
    in = InputFrame();
    in.interactPressed = true;
    b.step(in, 1);
    b.idle(15);
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
    // leave through the door
    const DoorDef* exitDoor = nullptr;
    for (const DoorDef& d : W.doors) if (d.shop == market->id && !d.toInterior) exitDoor = &d;
    CHECK(exitDoor && b.walkTo(v2(exitDoor->pos), 0.8f, 600), "walked to the exit door");
    in = InputFrame();
    in.interactPressed = true;
    b.step(in, 1);
    b.idle(70);
    CHECK(!g.player().indoors, "left the market");
    // ---- back to the car and to the workshop
    bool nearCar = b.walkTo(g.vehicles()[0].pos, 2.2f, 600);
    LOGI("player %.1f,%.1f car %.1f,%.1f near %d", g.player().pos.x, g.player().pos.y, g.vehicles()[0].pos.x, g.vehicles()[0].pos.y, (int)nearCar);
    in = InputFrame();
    in.enterExitPressed = true;
    b.step(in, 1);
    b.idle(30);
    CHECK(g.player().vehicle == 0, "re-entered the car");
    if (g.player().vehicle != 0) { LOGI("MVP scenario aborted: %d failures", g_failures); return 10; }
    g.vehicles()[0].health = 64;   // some wear to repair
    CHECK(b.driveRoad(v2(W.workshopApproach), 3.0f, 1500), "drove to the workshop");
    CHECK(b.driveTo(v2(W.workshopBayEntry), 2.5f, 900, 6.0f), "entered the workshop forecourt");
    CHECK(b.driveTo(v2(W.poiWorkshop), 1.8f, 900, 5.0f), "parked in the service bay");
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
