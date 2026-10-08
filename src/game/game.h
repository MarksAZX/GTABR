// Top-level game: world, entities, systems (interaction, economy, camera, NPC AI, save) and rendering/HUD.
#pragma once
#include <deque>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/jobs.h"
#include "../gfx/renderer.h"
#include "assets.h"
#include "camera.h"
#include "entities.h"
#include "input.h"
#include "items.h"
#include "navmesh.h"
#include "ui.h"
#include "../core/audio.h"
#include "model.h"
#include "weapons.h"
#include "timeofday.h"
#include "world.h"

namespace gtabr {

struct Settings {
  // graphics
  int quality = 2;          // 0 BAIXO, 1 MÉDIO, 2 ALTO, 3 ULTRA (see kQualityPresets)
  bool dynamicRes = true;   // lower the render scale when the frame time is over budget
  bool shadows = true;
  float drawDistance = 1.0f;   // multiplies the preset draw distance (props, NPCs, vehicles, HLOD switch)
  bool bloom = true;
  bool reflections = true;   // screen-space reflections on wet ground (Alto / Ultra)
  bool weatherFx = true;       // rain streaks / splashes (the weather itself still affects light and ground)
  float brightness = 1.0f;     // exposure multiplier
  bool showFps = false;
  // audio
  float master = 0.9f, sfx = 1.0f, ambience = 1.0f;
  bool muted = false;
  // controls
  float sensitivity = 1.0f;
  bool invertY = false;
  bool isometric = false;   // top-down view as a true isometric (45 degree, long lens) view
  float hudScale = 1.0f;
  float hudOpacity = 1.0f;
  bool aimAssist = true;
  // gameplay
  int weatherMode = 0;      // 0 automatic, 1 clear, 2 rain, 3 storm
  int dayCycle = 1;         // 0 frozen, 1 normal, 2 fast
  bool showMinimap = true;
  bool hints = true;
  bool autosave = true;
  // accessibility
  float textScale = 1.0f;
  bool highContrast = false;
  bool reduceMotion = false;   // no camera shake / speed zoom, calmer wind
  bool reduceFlashes = false;  // softer lightning and muzzle flashes
};

// Graphics presets: every field changes real work done per frame.
struct QualityPreset {
  const char* name;
  float renderScale;     // base resolution scale (dynamic resolution moves below it)
  int shadowMapSize;     // per cascade
  int shadowCascades;    // 0 = no sun shadows
  bool bloom;
  float drawDistance;    // metres, props/NPC/vehicle cull distance
  float lodBias;         // >1 picks coarser model LODs sooner
  int maxLights;         // dynamic point/spot lights
  float decorDensity;    // fraction of clutter props drawn
  int animFullRateNpcs;  // NPCs animated every frame; others at reduced rate
  float ao;              // screen-space ambient occlusion strength (0 = pass skipped)
  float shafts;          // volumetric light shaft intensity (0 = off)
};
inline const QualityPreset& qualityPreset(int q) {
  static const QualityPreset k[4] = {
      {"Baixo", 0.62f, 1024, 1, false, 70.0f, 1.8f, 6, 0.45f, 3, 0.0f, 0.0f},
      {"Médio", 0.80f, 1536, 1, true, 95.0f, 1.3f, 20, 0.7f, 6, 0.0f, 0.5f},
      {"Alto", 0.92f, 2048, 2, true, 125.0f, 1.0f, 48, 1.0f, 10, 0.55f, 0.8f},
      {"Ultra", 1.0f, 2048, 2, true, 160.0f, 0.7f, 96, 1.0f, 20, 0.8f, 1.0f},
  };
  return k[q < 0 ? 0 : (q > 3 ? 3 : q)];
}

struct Toast { std::string text; float t = 0; float dur = 2.6f; uint32_t color = 0xFFFFFFFFu; const char* icon = nullptr; };

struct Particle { Vec3 pos, vel; float life = 0, maxLife = 1, size = 1; uint32_t color = 0xFFFFFFFFu; float gravity = 0; };

enum class IKind { Npc, Vehicle, Door, Product, FuelPump, Workshop };
struct Interactable {
  IKind kind = IKind::Npc;
  int id = 0;           // npc index / vehicle index / door id / product id / pump id
  Vec3 pos;
  float radius = 2.0f;
  std::string label;    // verb shown on the prompt
  std::string sub;      // object name / price
  const char* icon = "hand";
  std::string art;      // optional product photo
  bool enabled = true;
  float dist = 0;
};

struct PanelOption {
  std::string label;
  std::string sub;
  const char* icon = nullptr;
  std::string art;
  bool enabled = true;
  bool closes = true;
  std::function<void()> action;
};

struct Panel {
  bool open = false;
  float anim = 0;
  std::string title, text, portrait;
  std::vector<PanelOption> options;
  std::string footer;
  float vehicleHealth = -1, vehicleFuel = -1, vehicleCap = 0;   // optional gauges
  std::function<void()> onClose;
  int scroll = 0;
};

enum class MenuState { None, Main, Slots, Pause };
enum class SlotMode { Load, New, Save };
constexpr int kSlots = 5;
struct SlotInfo {
  bool used = false;
  uint32_t seed = 0;
  int playSecs = 0, money = 0;
  std::string city, location;
  long long saved = 0;     // unix time of the last save
  float progress = 0;      // 0..1 (milestones reached)
};
// Progress milestones (bit index in Game::progress_).
enum Milestone { kPgFueled = 0, kPgBought, kPgRepaired, kPgSwam, kPgArmed, kPgShops, kPgEscaped, kPgDrove, kPgCount };

struct Waypoint { bool active = false; Vec3 pos; std::string name; };

// Per-actor animation inputs: a pending one-shot request (consumed), lying on the floor, held weapon.
struct AnimIn { int* req = nullptr; float reqSpeed = 1; bool reqUpper = false, reqHold = false; bool lying = false; int weapon = 0;
                bool airborne = false, dead = false, combat = false; float airPhase = 0; };

class Game {
 public:
  struct Init {
    gfx::Renderer* renderer = nullptr;
    JobSystem* jobs = nullptr;
    std::string saveDir;
    bool newGame = false;
    uint32_t seed = 0;   // city seed for a new game (0 = pick one)
    bool menu = false;   // show the main menu after loading (the app); tests start playing directly
    int slot = 1;        // active save slot when starting without the menu
  };
  bool init(const Init& i);
  void shutdown();
  void onTouch(int id, TouchAction a, float x, float y) { input_.onTouch(id, a, x, y); }
  // Advances the simulation and fills 'fd' (draw lists + HUD). dt in seconds (real time).
  void frame(float dt, gfx::FrameData& fd);
  void onBackground();  // app paused: persist state
  bool wantsQuit() const { return quit_; }
  void setScreenSize(float w, float h) { screenW_ = w; screenH_ = h; }

  // ---- test / scripting hooks (used by headless tests and the demo driver)
  void injectInput(const InputFrame& f) { scripted_ = f; useScripted_ = true; }
  void clearInjected() { useScripted_ = false; }
  bool loaded() const { return phase_ == Phase::Playing || phase_ == Phase::Menu; }
  bool inMenu() const { return phase_ == Phase::Menu; }
  bool playing() const { return phase_ == Phase::Playing; }
  int residentChunks() const { return stats_.residentChunks; }
  int drawnChunks() const { return stats_.drawnChunks; }
  bool switching() const { return phase_ == Phase::Switching; }

  // ---- state access (read by tests)
  Player& player() { return player_; }
  std::vector<Vehicle>& vehicles() { return vehicles_; }
  std::vector<Npc>& npcs() { return npcs_; }
  int& money() { return moneyCents_; }
  int itemCount(int id) const { return inventory_[id]; }
  CameraRig& camera() { return cam_; }
  // position of the (active) pickup of a weapon, or false
  bool pickupPos(int weapon, Vec3& out) const {
    for (const auto& k : pickups_) if (k.weapon == weapon && k.active) { out = k.pos; return true; }
    return false;
  }
  const World& world() const { return world_; }
  Panel& panel() { return panel_; }
  std::vector<Interactable>& focusList() { return nearby_; }
  const Interactable* focus() const { return focus_.id >= 0 || focusValid_ ? &focus_ : nullptr; }
  bool focusValid() const { return focusValid_; }
  MenuState menu() const { return menu_; }

  // Gameplay commands (also used by the HUD buttons)
  void activateInteractable(const Interactable& it);
  void toggleCamera();
  void tryEnterExit();
  bool saveGame();                 // writes the active slot
  bool loadGame();                 // reads the active slot (same city)
  // ---- slots / menus (game_save.cpp, game_menu.cpp)
  SlotInfo readSlotInfo(int slot) const;
  int latestSlot() const;          // most recently saved slot (0 = none)
  bool saveToSlot(int slot);
  bool deleteSlot(int slot);
  void startNewGame(int slot, uint32_t seed = 0);
  bool loadSlot(int slot);
  void enterMainMenu();
  void openMenu(MenuState m) { menu_ = m; }
  void tapUi(int id) { menuAction(id); }          // test hook: same path as a finger tap
  uint32_t seed() const { return worldSeed_; }
  uint32_t progress() const { return progress_; }
  int activeSlot() const { return activeSlot_; }
  void markProgress(int bit) { if (!(progress_ & (1u << bit))) { progress_ |= 1u << bit; } }
  void runCode(int code);          // pause-menu codes (cheats), each with a real effect
  bool menuIsOpen() const { return menu_ != MenuState::None; }
  std::string locationName(Vec2 p, bool indoors) const;
  bool loadSettings();
  bool saveSettings() const;
  void startFueling(int vehicleIdx, int pumpId, int amountCents /*0 = fill*/);
  void buyItem(int item, bool fromShelf, int priceCents = -1);
  bool buyWeapon(int weapon, int priceCents);
  void buyStock(int shopId, int stockIdx);
  int shopPriceCents(int shopId, int item) const;
  void openShopPanel(int shopId);
  void openAttendantPanel(int shopId);
  void repairVehicle(int vehicleIdx);
  void useItem(int item);
  void openPauseMenu() { menu_ = MenuState::Pause; }
  void selectPanelOptionPublic(int idx) { selectPanelOption(idx); }
  float fuelPriceCents() const { return 629.0f; }
  float repairCostCents(const Vehicle& v) const;
  int nearestVehicleTo(Vec2 p, float maxDist, bool onlyDrivable = true) const;
  void toast(const std::string& s, const char* icon = nullptr, uint32_t color = 0xFFFFFFFFu);
  void teleportPlayer(Vec2 p, float yaw);
  bool playerInVehicle() const { return player_.vehicle >= 0; }
  int pumpNearVehicle(const Vehicle& v) const;
  Settings& settings() { return settings_; }
  float elapsed() const { return time_; }
  void setDebugOverlay(bool b) { settings_.showFps = b; }

 private:
  enum class Phase { Loading, Menu, Playing, Switching };

  // ---- core (game_core.cpp)
  void startWorldJob();
  void finishLoading();
  void resetEntities(bool fresh);
  void updatePlaying(float dt, const InputFrame& in);
  void updatePlayer(float dt, const InputFrame& in);
  void updateVehicles(float dt, const InputFrame& in);
  void updateInteractions(const InputFrame& in);
  void updateParticles(float dt);
  void updateAmbientFx(float dt);   // wind-blown leaves, dust motes in sunlight, exhaust of moving cars
  void updateAdaptiveQuality(float dt);
  void applySettings();
  void collectInteractables();
  void applyItemEffects(const ItemDef& d);

  // ---- NPC (game_npc.cpp)
  void spawnNpcs();
  void updateNpcs(float dt);
  void npcThink(Npc& n, float dt);
  const NavMesh& navFor(const Npc& n) const { return n.interior ? navIndoor_ : navOutdoor_; }

  // ---- panels / dialogs (game_actions.cpp)
  void openPanel(Panel p);
  void closePanel();
  void openFuelPanel(int pumpId);
  void openWorkshopPanel();
  void openNpcPanel(int npcIdx);
  void openSettingsMenu() { settingsOnly_ = false; pauseTab_ = 4; menu_ = MenuState::Pause; }

  // ---- rendering (game_render.cpp)
  void buildScene(gfx::FrameData& fd);
  void emitWorld(gfx::FrameData& fd);
  void emitSprites(gfx::FrameData& fd);
  void emitSpriteSet(const std::string& lowName, const std::string& highName, Vec3 pos, float scale, uint32_t tint, float alphaMul, bool mirror, bool silhouette, bool shadowDecal);
  void flushSprites(gfx::FrameData& fd);
  void setupGlobals(gfx::FrameData& fd);
 public:
  void setTimeOfDay(float h, float rate) { timeOfDay_ = h; dayRate_ = rate; dayRateOverridden_ = true; }
  void setWeatherMode(int mode);
  int weatherMode() const { return weatherMode_; }
  float rainAmount() const { return rain_; }
  float wetness() const { return wetness_; }
  float timeOfDay() const { return timeOfDay_; }
  void setQuality(int q) { settings_.quality = q; applySettings(); }
  // test / tooling hook: put an extra pedestrian of the given archetype in the world
  int debugSpawnNpc(const std::string& archetype, Vec2 pos, float yaw, bool walk = false);
  void setIsometric(bool on) { settings_.isometric = on; applySettings(); }
 private:
  int dirIndex(float objYaw, int n) const;

  // ---- UI (game_ui.cpp)
  void buildUi(gfx::FrameData& fd, float dt);
  void drawLoading(float dt);
  void drawHud(float dt, const InputFrame& in);
  void drawTouchControls(const InputFrame& in);
  void drawMinimap();
  void drawBars();
  void drawPrompt();
  void drawToasts(float dt);
  void drawMoney();
  void drawFuelGauge();
  void drawPanel(float dt);
  void drawWheel(float dt);
  void drawDebug();
  InputLayout makeLayout() const;
  void handleUiPointers(const InputFrame& in);
  float uiScale() const;

  // ---- save (game_save.cpp)

  Init cfg_;
  gfx::Renderer* r_ = nullptr;
  JobSystem* jobs_ = nullptr;
  Assets assets_;
  Phase phase_ = Phase::Loading;
  World world_;
  std::atomic<bool> worldReady_{false};
  bool entitiesReady_ = false;
  NavMesh navOutdoor_, navIndoor_;
  gfx::TexHandle mapTex_;
  gfx::TexHandle probeTex_;   // baked ambient visibility probes of the current city
  float mapExtent_ = 96.0f;
  uint32_t worldSeed_ = 1;      // seed of the current city (saved per slot)
  float lodDistance_ = 1e9f;  // chunks farther than this draw their HLOD mesh
  CameraRig cam_;
  InputSystem input_;
  InputFrame scripted_;
  InputFrame visualInput_;     // last polled input (joystick / button visuals)
  bool useScripted_ = false;
  Settings settings_;
  UiPainter ui_;
  gfx::FrameData* fd_ = nullptr;

  float screenW_ = 1920, screenH_ = 1080;
  float time_ = 0;
  float realTime_ = 0;
  float timeScale_ = 1.0f;
  float loadingAnim_ = 0;
  bool quit_ = false;

  Player player_;
  std::vector<Vehicle> vehicles_;
  std::vector<Npc> npcs_;
  int moneyCents_ = 40000;
  int inventory_[kItemCount] = {};
  float moneyShow_ = 0;           // seconds the money widget stays visible
  float moneyDisplay_ = 40000;    // animated value
  Rng rng_{12345};

  // fuelling
  struct Fueling { bool active = false; int vehicle = -1; float litersLeft = 0; float rate = 12.0f; float total = 0; } fueling_;

  std::vector<Interactable> nearby_;
  Interactable focus_;
  bool focusValid_ = false;
  Waypoint waypoint_;

  std::deque<Toast> toasts_;
  Pool<Particle> particles_{256};
  Panel panel_;
  std::vector<std::pair<Vec4, int>> uiRects_;   // clickable rects built while drawing panels/menus: (x,y,w,h) -> index
  float panelScrollVel_ = 0;
  int pressedUi_ = -1;
  float sliderDrag_ = -1;
  int activeSlider_ = -1;

  // wheel
  struct Wheel {
    bool open = false;
    float anim = 0;
    int category = 1;        // 0 weapons, 1 items
    int hovered = -1;        // slot index in the current category list
    int hoveredCategory = -1;
    Vec2 finger;
    std::vector<int> slots[2];
    float catAnim[2] = {0, 1};
  } wheel_;
  float blur_ = 0;

  // HUD timers
  float healthShow_ = 0, staminaShow_ = 3;
  float lastHealth_ = 100;
  float fadeAlpha_ = 0;       // scene fade (interior transitions)
  float fadeTarget_ = 0;
  std::function<void()> fadeThen_;
  float autosave_ = 0;
  float fpsAccum_ = 0; int fpsFrames_ = 0; float fpsShown_ = 60; float frameMsAvg_ = 16.0f;
  float adaptTimer_ = 0;
  bool wasWheelHeld_ = false;
  float promptAnim_ = 0;
  float cameraShake_ = 0;
  int tutorialStep_ = 0;
  float tutorialTimer_ = 0;
  float neighbourCooldown_ = 0;
  Vec2 lastFocusPos_;

  // render scratch
  std::map<int, std::vector<gfx::SpriteInst>> spriteBuckets_;   // key: texture id (+100000 for the crossfade layer)
  std::map<int, std::vector<gfx::SpriteInst>> silBuckets_;
  std::vector<gfx::DecalInst> decals_;
  float spriteWeightHigh_ = 0;
  float indoorBlend_ = 0;
  gfx::TexHandle materials_;
  gfx::MaterialHandle worldMaterial_;
  Vec3 sunDir_{-0.50f, 0.74f, 0.44f};
  // time of day / weather
  float timeOfDay_ = 10.0f;      // hours
  float dayRate_ = 1.0f / 60.0f; // game hours per real second (a full day in 24 minutes)
  bool dayRateOverridden_ = false;   // a test pinned the clock
  float cloudCover_ = 0.42f;
  float wetness_ = 0.0f;
  // weather (see game_weather.cpp)
  int weatherMode_ = 0;           // 0 automatic, 1 clear, 2 rain, 3 storm
  float weatherTarget_ = 0.0f, rain_ = 0.0f, wind_ = 0.3f, weatherTimer_ = 90.0f, flash_ = 0.0f, flashTimer_ = 8.0f, thunderIn_ = 0.0f;
  int rainHandle_ = 0;
  Rng wRng_{0xBADC0FFEE0DDF00Dull};   // weather has its own stream so it never perturbs gameplay randomness

  // ---- menus, slots and world switching (game_menu.cpp / game_save.cpp / game_core.cpp)
  void menuAction(int id);
  void drawMainMenu(float dt);
  void drawPauseMenu(float dt);
  void drawSlotList(float x, float y, float w, float h, SlotMode mode);
  void drawConfirm();
  void drawMapTab(float x, float y, float w, float h);
  void drawGameTab(float x, float y, float w, float h);
  void drawInventoryTab(float x, float y, float w, float h);
  void drawCodesTab(float x, float y, float w, float h);
  void drawSettingsTab(float x, float y, float w, float h);
  void drawSaveTab(float x, float y, float w, float h);
  void handleMenuPointers(const InputFrame& in);
  void updateMenuScene(float dt);
  void refreshSlots();
  void applyStateFromFile(const std::unordered_map<std::string, std::string>& kv);
  void buildWorldGpu();
  void streamChunks(Vec3 focus, int budget);   // keeps GPU meshes resident near the focus only (budgeted per frame)
  void destroyWorldGpu();
  void switchWorld(uint32_t seed, std::function<void()> then);
  void beginPlaying(bool fresh);
  void applyAudioSettings();
  void showConfirm(const std::string& title, const std::string& text, const std::string& yes, const std::string& no, std::function<void()> onYes);
  std::string slotPath(int slot) const;
  void applySettingStep(int id);
  void applySettingSlider(int id, float t);
  MenuState menu_ = MenuState::None;
  int pauseTab_ = 0, settingsTab_ = 0;
  bool settingsOnly_ = false;           // settings opened from the main menu (no pause tabs)
  SlotMode slotMode_ = SlotMode::Load;
  SlotInfo slots_[kSlots];
  int activeSlot_ = 1;
  uint32_t progress_ = 0, shopsVisited_ = 0;
  float driven_ = 0;                    // metres driven
  struct Confirm { bool open = false; std::string title, text, yes, no; std::function<void()> onYes; } confirm_;
  std::function<void()> afterWorld_;
  std::string codeStatus_;
  float codeStatusT_ = 0;
  float menuT_ = 0;                     // time since the current menu opened (fade-in)
  float menuCamT_ = 0;
  // full map view
  Vec2 mapCenter_;
  float mapZoom_ = 0;                   // pixels per metre (0 = fit)
  Vec2 mapDragPrev_;
  bool mapDragging_ = false, mapMoved_ = false;
  float mapPinchPrev_ = 0;
  Vec4 mapRect_;
  float uiMoved_ = 0;                   // pointer travel since press (tap vs drag)
  DayLighting day_;
  Vec3 shadowFocus_;
  float shadowRadius_ = 70.0f;
  const QualityPreset& preset() const { return qualityPreset(settings_.quality); }
  const SpriteDef* charSprite(const std::string& arch, int frame, const char* anim, int dir, bool high) const;
  // pre-resolved sprite tables (built once after loading)
  static constexpr int kArch = 10;
  struct CharSprites { int dirs = 8; const SpriteDef* s[2][3][8][16] = {}; };
  struct VehSprites { const SpriteDef* s[2][32] = {}; };
  struct DecorSprites { int dirs = 4; const SpriteDef* s[2][8] = {}; };
  CharSprites charSpr_[kArch];
  VehSprites vehSpr_[3][3];
  std::unordered_map<std::string, DecorSprites> decorSpr_;
  const DecorSprites* decorRefs_[1] = {nullptr};
  std::vector<const DecorSprites*> decorOf_;     // parallel to world_.decor
  int archIndex(const std::string& a) const;
  void buildSpriteTables();
  float pitchHighWeight() const;
  void updateWeather(float dt);
  void applyWeatherToLighting(DayLighting& d) const;
  void emitRain();
  void addSprite(const SpriteDef* d, Vec3 pos, float scale, float alpha, bool mirror, uint32_t rgb, bool silhouette, bool secondary,
                 float emissive = 0.0f);
  // ---- combat (game_combat.cpp)
 public:
  void applyDamage(ActorRef target, const DamageInfo& d);
  void giveWeapon(int weapon, int ammo);
  void equipWeapon(int weapon);
  int wantedLevel() const { return wanted_; }
  Audio& audio() { return audio_; }
  bool playerDead() const { return player_.dead; }
  int aliveCops() const;
  int copsChasing() const;
  float sinceCopsSawPlayer() const { return sinceSeen_; }
 private:
  void updateCombat(float dt, const InputFrame& in);
  void updatePlayerAttack(float dt, const InputFrame& in);
  void startMelee(ActorRef who, int kind);
  bool meleeStrike(ActorRef attacker, Vec2 pos, float yaw, int weapon, int kind);
  void fireWeapon(ActorRef shooter, Vec3 origin, Vec2 dir, int weapon);
  int findAimTarget(Vec2 from, Vec2 dir, float range, float cosCone, bool preferHostile) const;
  bool lineOfSight(Vec2 a, Vec2 b, float height = 1.3f) const;
  Vec2 actorPos(ActorRef r) const;
  bool actorAlive(ActorRef r) const;
  void emitEvent(EventKind k, Vec2 pos, float radius, ActorRef who, ActorRef victim, float severity);
  void updateEffects(float dt);
  void updatePickups(float dt);
  void emitCombatVisuals(gfx::FrameData& fd);
  void killPlayer(const DamageInfo& d);
  void respawnPlayer();
  void spawnBlood(Vec3 p, Vec2 dir, int n);
  void spawnImpact(Vec3 p, int n);
  void spawnSplash(Vec3 p, int n, float power);
  void spawnFoam(Vec3 p, int n, float size);
  float splashT_ = 0;
  void requestAnim(Player& p, int act, float speed = 1, bool upper = false, bool hold = false) { p.animReq = act; p.animReqSpeed = speed; p.animReqUpper = upper; p.animReqHold = hold; }
  void requestAnim(Npc& n, int act, float speed = 1, bool upper = false, bool hold = false) { n.animReq = act; n.animReqSpeed = speed; n.animReqUpper = upper; n.animReqHold = hold; }
  // ---- NPC behaviour (game_npc.cpp)
  void perceiveEvents(Npc& n);
  void npcFight(Npc& n, float dt);
  void npcStartFlee(Npc& n, Vec2 from, float secs);
  std::string npcLine(Npc& n, int context);
  void npcSay(Npc& n, const std::string& line, float secs = 2.4f);
  // ---- police / wanted (game_police.cpp)
  void reportCrime(Vec2 where, float severity, bool witnessedByCop);
  void updateWanted(float dt);
  void updatePolice(float dt);
  void copThink(Npc& c, float dt);
  void spawnPoliceUnit(Vec2 dest, bool onFoot);
  void driveAi(Vehicle& v, Vec2 target, float maxSpeed, float dt);
  void aiCarContacts(Vehicle& v);
  // ---- ambient traffic (game_traffic.cpp)
  void spawnTraffic();
  bool spawnTrafficCar(bool farFromPlayer);
  void planTrafficRoute(Vehicle& v);
  std::vector<Vec2> laneRoute(Vec2 here, Vec2 dest, Vec2 headingHint = {0, 0}) const;
  void updateTraffic(float dt);
  float trafficRespawnT_ = 0;
  bool menuStreamFirst_ = true;
  bool copCanSee(const Npc& c, Vec2 target) const;
  Vec2 roadPointNear(Vec2 p) const;

  struct Tracer { Vec3 a, b; float life; };
  struct Stain { Vec3 pos; float r, life; };
  struct Pickup { int weapon; int ammo; Vec3 pos; bool active = true; float respawn = 0; };
  Audio audio_;
  WeaponMeshes weaponMeshes_;
  std::vector<WorldEvent> events_;
  std::vector<Tracer> tracers_;
  std::vector<Stain> stains_;
  std::vector<Pickup> pickups_;
  int wanted_ = 0;
  float wantedHeat_ = 0;      // accumulated crime weight
  float sinceSeen_ = 1e9f;    // seconds since any officer saw the player
  Vec2 wantedLastKnown_;
  float evadeT_ = 0;          // search phase timer when no cop sees the player
  float policeSpawnT_ = 0;
  float lastReportT_ = -100.0f, lastReportSev_ = 0;
  Vec2 lastReportPos_;
  int sirenHandle_ = 0;
  int surfHandle_ = 0;   // ambient breaking waves near the coast
  float deathT_ = 0;
  float slowMo_ = 1.0f;
  float muzzleT_ = 0;
  Vec3 muzzlePos_;
  bool attackBtnPrev_ = false;
  // ---- 3D models (game_models.cpp)
  void queueModels();
  void finishModels();
  void emitModels(gfx::FrameData& fd, float dt);
  const ModelAsset* charModel(const std::string& name) const;
  const ModelAsset* modelForArchetype(const std::string& a, int id) const;
  int modelLod(const ModelAsset& m, float dist) const;
  void emitCharacter(gfx::FrameData& fd, const ModelAsset& m, CharAnim& a, Vec3 pos, float yaw, float scale, float speed, float dt,
                     bool fullRate, Vec4 tint, const AnimIn& in = AnimIn{});
  void emitVehicle(gfx::FrameData& fd, int model, int color, Vec3 pos, float yaw, float pitch, float roll, float steer, float spin,
                   bool lightsOn, bool braking, int signal, bool reversing);
  struct PendingLight { Vec3 pos, dir, color; float radius, cone, dist; };
  std::vector<ModelAsset> charModels_, carModels_;
  std::vector<AnimClip> clips_;
  Animator animator_;
  CharAnim playerAnim_;
  std::vector<CharAnim> npcAnim_;
  gfx::ModelHandle wheelModel_;
  gfx::MaterialHandle wheelMaterial_;
  bool modelsReady_ = false, carsReady_ = false;
  std::vector<PendingLight> pendingLights_;
  std::unordered_map<int, bool> npcModelDrawn_;
  float interactPulse_ = 0;
  float lastDt_ = 1.0f / 30.0f;
  void addDecalEllipse(Vec3 pos, float hx, float hz, float alpha, float yaw, float kind);
  void projectToScreen(const Vec3& p, Vec2& out, bool& visible) const;
  void selectPanelOption(int idx);
  struct Stat { int residentChunks = 0, drawnChunks = 0, drawnSprites = 0, drawnModels = 0, npcNear = 0, npcMid = 0, npcFar = 0; } stats_;
};

}  // namespace gtabr
