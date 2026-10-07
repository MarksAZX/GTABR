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
#include "model.h"
#include "timeofday.h"
#include "world.h"

namespace gtabr {

struct Settings {
  float sensitivity = 1.0f;
  bool invertY = false;
  bool shadows = true;
  int quality = 2;          // 0 BAIXO, 1 MÉDIO, 2 ALTO, 3 ULTRA (see kQualityPresets)
  bool dynamicRes = true;   // lower the render scale when the frame time is over budget
  float hudScale = 1.0f;
  bool showFps = false;
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
};
inline const QualityPreset& qualityPreset(int q) {
  static const QualityPreset k[4] = {
      {"Baixo", 0.62f, 1024, 1, false, 70.0f, 1.8f, 4, 0.45f, 3},
      {"Médio", 0.80f, 1536, 1, true, 95.0f, 1.3f, 8, 0.7f, 6},
      {"Alto", 0.92f, 2048, 2, true, 125.0f, 1.0f, 12, 1.0f, 10},
      {"Ultra", 1.0f, 2048, 2, true, 160.0f, 0.7f, 16, 1.0f, 20},
  };
  return k[q < 0 ? 0 : (q > 3 ? 3 : q)];
}

struct Toast { std::string text; float t = 0; float dur = 2.6f; uint32_t color = 0xFFFFFFFFu; const char* icon = nullptr; };

struct Particle { Vec3 pos, vel; float life = 0, maxLife = 1, size = 1; uint32_t color = 0xFFFFFFFFu; };

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

enum class MenuState { None, Pause, Settings };

struct Waypoint { bool active = false; Vec3 pos; std::string name; };

class Game {
 public:
  struct Init {
    gfx::Renderer* renderer = nullptr;
    JobSystem* jobs = nullptr;
    std::string saveDir;
    bool newGame = false;
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
  bool loaded() const { return phase_ == Phase::Playing; }

  // ---- state access (read by tests)
  Player& player() { return player_; }
  std::vector<Vehicle>& vehicles() { return vehicles_; }
  std::vector<Npc>& npcs() { return npcs_; }
  int& money() { return moneyCents_; }
  int itemCount(int id) const { return inventory_[id]; }
  CameraRig& camera() { return cam_; }
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
  bool saveGame();
  bool loadGame();
  void startFueling(int vehicleIdx, int pumpId, int amountCents /*0 = fill*/);
  void buyItem(int item, bool fromShelf);
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
  enum class Phase { Loading, Playing };

  // ---- core (game_core.cpp)
  void startWorldJob();
  void finishLoading();
  void resetEntities(bool fresh);
  void updatePlaying(float dt, const InputFrame& in);
  void updatePlayer(float dt, const InputFrame& in);
  void updateVehicles(float dt, const InputFrame& in);
  void updateInteractions(const InputFrame& in);
  void updateParticles(float dt);
  void updateAdaptiveQuality(float dt);
  void applySettings();
  void collectInteractables();
  void applyItemEffects(const ItemDef& d);

  // ---- NPC (game_npc.cpp)
  void spawnNpcs();
  void updateNpcs(float dt);
  void npcThink(Npc& n, float dt);
  const NavMesh& navFor(const Npc& n) const { return n.interior ? navIndoor_ : navOutdoor_; }
  std::string chatLine(const Npc& n);

  // ---- panels / dialogs (game_actions.cpp)
  void openPanel(Panel p);
  void closePanel();
  void openFuelPanel(int pumpId);
  void openShopPanel(const char* portrait);
  void openAttendantPanel();
  void openWorkshopPanel();
  void openNpcPanel(int npcIdx);
  void openSettingsMenu();

  // ---- rendering (game_render.cpp)
  void buildScene(gfx::FrameData& fd);
  void emitWorld(gfx::FrameData& fd);
  void emitSprites(gfx::FrameData& fd);
  void emitSpriteSet(const std::string& lowName, const std::string& highName, Vec3 pos, float scale, uint32_t tint, float alphaMul, bool mirror, bool silhouette, bool shadowDecal);
  void flushSprites(gfx::FrameData& fd);
  void setupGlobals(gfx::FrameData& fd);
 public:
  void setTimeOfDay(float h, float rate) { timeOfDay_ = h; dayRate_ = rate; }
  float timeOfDay() const { return timeOfDay_; }
  void setQuality(int q) { settings_.quality = q; applySettings(); }
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
  void drawMenus(float dt);
  void drawDebug();
  InputLayout makeLayout() const;
  void handleUiPointers(const InputFrame& in);
  float uiScale() const;

  // ---- save (game_save.cpp)
  std::string savePath() const;

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
  float mapExtent_ = 96.0f;
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
  MenuState menu_ = MenuState::None;
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
  float cloudCover_ = 0.42f;
  float wetness_ = 0.0f;
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
  void addSprite(const SpriteDef* d, Vec3 pos, float scale, float alpha, bool mirror, uint32_t rgb, bool silhouette, bool secondary,
                 float emissive = 0.0f);
  // ---- 3D models (game_models.cpp)
  void queueModels();
  void finishModels();
  void emitModels(gfx::FrameData& fd, float dt);
  const ModelAsset* charModel(const std::string& name) const;
  const ModelAsset* modelForArchetype(const std::string& a, int id) const;
  int modelLod(const ModelAsset& m, float dist) const;
  void emitCharacter(gfx::FrameData& fd, const ModelAsset& m, CharAnim& a, Vec3 pos, float yaw, float scale, float speed, float dt,
                     bool fullRate, Vec4 tint);
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
  struct Stat { int drawnChunks = 0, drawnSprites = 0, drawnModels = 0, npcNear = 0, npcMid = 0, npcFar = 0; } stats_;
};

}  // namespace gtabr
