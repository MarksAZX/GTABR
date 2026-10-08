// HUD, touch controls, minimap, panels, radial wheel, pause/settings menus.
#include <algorithm>
#include <cmath>

#include "game.h"

namespace gtabr {

namespace {
const Color kWhite = 0xFFFFFFFFu;
Color C(float r, float g, float b, float a = 1.0f) { return rgba(r, g, b, a); }
const Color kAccent = rgba(0.78f, 0.82f, 0.84f);
const Color kGlass = rgba(0.045f, 0.055f, 0.08f, 0.60f);
const Color kGlassHi = rgba(0.10f, 0.12f, 0.17f, 0.72f);
const Color kMint = rgba(0.60f, 0.72f, 0.66f);
const Color kRed = rgba(1.0f, 0.36f, 0.40f);
const Color kSky = rgba(0.58f, 0.67f, 0.76f);
const Color kMuted = rgba(1.0f, 1.0f, 1.0f, 0.62f);

std::vector<std::string> wrapText(const UiPainter& ui, const std::string& s, float maxW, float size, bool bold) {
  std::vector<std::string> lines;
  std::string cur, word;
  auto flushWord = [&]() {
    if (word.empty()) return;
    std::string trial = cur.empty() ? word : cur + " " + word;
    if (ui.textWidth(bold, trial, size) > maxW && !cur.empty()) { lines.push_back(cur); cur = word; }
    else cur = trial;
    word.clear();
  };
  for (char c : s) {
    if(c=='\n'){flushWord();if(!cur.empty()){lines.push_back(cur);cur.clear();}}
    else if (c == ' ') flushWord();
    else word.push_back(c);
  }
  flushWord();
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}
}  // namespace

float Game::uiScale() const { return std::max(0.55f, screenH_ / 1080.0f * settings_.hudScale); }

InputLayout Game::makeLayout() const {
  InputLayout L;
  float S = uiScale()*settings_.controlScale;
  L.width = screenW_; L.height = screenH_;
  L.joyZoneRight = screenW_ * 0.44f;
  L.joyRadius = 105.0f * S;
  L.joyFixed=settings_.fixedJoystick;L.leftHanded=settings_.leftHanded;
  L.joyCenter={L.joyRadius*1.6f,screenH_-L.joyRadius*1.9f};
  // right thumb cluster: the primary action (attack / shoot) is the big button; secondary ones are smaller and
  // contextual (interact, enter/exit, reload only show up when they can be used)
  Vec2 A{screenW_ - 210.0f * S, screenH_ - 190.0f * S};
  bool driving = player_.vehicle >= 0;
  L.attack = {A + Vec2{40, 40} * S, 60.0f * S, !driving && !player_.dead && !player_.swimming};
  L.run = {A + Vec2{-128, 92} * S, 44.0f * S, true};
  L.interact = {A + Vec2{-138, -46} * S, 56.0f * S, focusValid_};
  L.enterExit = {A + Vec2{36, -122} * S, 54.0f * S, driving || nearestVehicleTo(player_.pos, 2.4f) >= 0};
  L.reload = {A + Vec2{-70, -150} * S, 42.0f * S,
              !driving && isFirearm(player_.weapon) && player_.mag[player_.weapon] < weaponDef(player_.weapon).magazine &&
                  player_.reserve[player_.weapon] > 0};
  L.camera = {A + Vec2{-270, 96} * S, 40.0f * S, true};
  L.wheel = {A + Vec2{-262, -44} * S, 46.0f * S, true};
  L.pause = {Vec2{screenW_ - 64.0f * S, 64.0f * S}, 34.0f * S, true};
  HudButton* controls[]={&L.run,&L.interact,&L.enterExit,&L.camera,&L.wheel,&L.pause,&L.attack,&L.reload};
  for(int i=0;i<8;++i){auto& b=*controls[i];if(settings_.leftHanded)b.c.x=screenW_-b.c.x;
    if(settings_.controlPos[i].x>=0&&settings_.controlPos[i].y>=0)b.c={settings_.controlPos[i].x*screenW_,settings_.controlPos[i].y*screenH_};
    b.c.x=clamp(b.c.x,b.r+8,screenW_-b.r-8);b.c.y=clamp(b.c.y,b.r+8,screenH_-b.r-8);}
  if(settings_.leftHanded)L.joyCenter.x=screenW_-L.joyCenter.x;
  if(settings_.controlPos[8].x>=0&&settings_.controlPos[8].y>=0)L.joyCenter={settings_.controlPos[8].x*screenW_,settings_.controlPos[8].y*screenH_};
  L.joyCenter.x=clamp(L.joyCenter.x,L.joyRadius+8,screenW_-L.joyRadius-8);L.joyCenter.y=clamp(L.joyCenter.y,L.joyRadius+8,screenH_-L.joyRadius-8);
  L.modal = panel_.open || menu_ != MenuState::None;
  if (L.modal) {
    L.run.visible = L.interact.visible = L.enterExit.visible = L.camera.visible = L.wheel.visible = L.pause.visible = false;
    L.attack.visible = L.reload.visible = false;
  }
  return L;
}

// ------------------------------------------------------------------------------------------------ loading
void Game::drawLoading(float dt) {
  (void)dt;
  float S = uiScale();
  ui_.gradient(0, 0, screenW_, screenH_, C(0.05f, 0.07f, 0.12f), C(0.02f, 0.025f, 0.04f));
  float cx = screenW_ * 0.5f, cy = screenH_ * 0.5f;
  bool hasFont = assets_.fontBold.tex.valid();
  if (hasFont) {
    ui_.text(true, "BAIRRO", cx, cy - 120 * S, 120 * S, kWhite, Align::Center);
    ui_.text(false, "Uma cidade. A sua história.", cx, cy + 20 * S, 32 * S, kMuted, Align::Center);
  }
  float p = assets_.progress() * 0.85f + (worldReady_ ? 0.15f : 0.0f);
  float bw = 520 * S, bh = 8 * S;
  ui_.rect(cx - bw / 2, cy + 100 * S, bw, bh, C(1, 1, 1, 0.14f), bh / 2);
  ui_.rect(cx - bw / 2, cy + 100 * S, bw * clamp(p, 0.02f, 1.0f), bh, kAccent, bh / 2);
  if (hasFont) {
    static const char* tips[] = {"Dica: o botão de câmera alterna entre Top Down e Terceira Pessoa.", "Dica: encontre os estabelecimentos pelo mapa no menu de pausa.",
                                 "Dica: segure o botão ITENS para abrir a roda de itens.", "Dica: se o tanque secar, empurre... ou ande até o posto!"};
    ui_.text(false, tips[(int)(loadingAnim_ / 3.0f) % 4], cx, cy + 140 * S, 24 * S, C(1, 1, 1, 0.5f), Align::Center);
  }
}

// ------------------------------------------------------------------------------------------------ HUD pieces
static void glassButton(UiPainter& ui, Vec2 c, float r, bool pressed, bool active, const char* icon, Color accent, float alpha, float iconScale = 0.95f) {
  ui.glow(c.x - r, c.y - r + 6, r * 2, r * 2, r, r * 0.45f, C(0, 0, 0, 0.30f * alpha));
  Color fill = pressed ? C(1, 1, 1, 0.30f * alpha) : C(0.04f, 0.05f, 0.08f, 0.42f * alpha);
  if (active) fill = mixColor(fill, withAlpha(accent, 0.35f * alpha), 0.6f);
  ui.circle(c.x, c.y, r, fill, std::max(1.5f, r * 0.03f), active ? withAlpha(accent, 0.95f * alpha) : C(1, 1, 1, 0.34f * alpha));
  ui.icon(icon, c.x, c.y, r * iconScale, active ? withAlpha(accent, alpha) : C(1, 1, 1, 0.95f * alpha));
}

void Game::drawTouchControls(const InputFrame& in) {
  InputLayout L = makeLayout();
  float S = uiScale();
  float a = (1.0f - 0.85f * wheel_.anim)*settings_.controlOpacity;
  bool driving = player_.vehicle >= 0;
  // joystick
  if (in.joyActive) {
    ui_.circle(in.joyBase.x, in.joyBase.y, L.joyRadius, C(0.05f, 0.06f, 0.09f, 0.28f * a), 2.0f * S, C(1, 1, 1, 0.38f * a));
    ui_.circle(in.joyKnob.x, in.joyKnob.y, L.joyRadius * 0.42f, C(1, 1, 1, 0.38f * a), 2.0f * S, C(1, 1, 1, 0.8f * a));
  } else {
    Vec2 h=L.joyCenter;
    ui_.circle(h.x, h.y, L.joyRadius, C(0.05f, 0.06f, 0.09f, 0.16f * a), 2.0f * S, C(1, 1, 1, 0.20f * a));
    ui_.circle(h.x, h.y, L.joyRadius * 0.42f, C(1, 1, 1, 0.14f * a), 0, 0);
  }
  glassButton(ui_, L.run.c, L.run.r, in.runHeld, false, driving ? "target" : "run", kAccent, a, 0.72f);
  ui_.text(false, driving ? "FREIO" : "CORRER", L.run.c.x, L.run.c.y + L.run.r * 0.5f, 14 * S, C(1, 1, 1, 0.7f * a), Align::Center);
  // primary action: attack with the current weapon (fist / melee / firearm)
  if (L.attack.visible) {
    const WeaponDef& w = weaponDef(player_.weapon);
    bool gun = w.magazine > 0;
    bool empty = gun && player_.mag[player_.weapon] == 0 && player_.reserve[player_.weapon] == 0;
    glassButton(ui_, L.attack.c, L.attack.r, in.attackHeld, player_.attackT >= 0 || player_.fireCooldown > 0.05f, w.icon, gun ? kRed : kAccent,
                empty ? a * 0.5f : a, 0.62f);
    if (gun) {
      std::string ammo = std::to_string(player_.mag[player_.weapon]) + " | " + std::to_string(player_.reserve[player_.weapon]);
      ui_.text(true, ammo, L.attack.c.x, L.attack.c.y + L.attack.r * 0.46f, 17 * S, C(1, 1, 1, 0.85f * a), Align::Center);
      if (player_.reloadT >= 0) {
        float k = player_.reloadT / w.reloadTime;
        ui_.arc(L.attack.c.x, L.attack.c.y, L.attack.r + 3 * S, L.attack.r + 8 * S, 0, kTau * k, withAlpha(kAccent, 0.9f * a));
      }
    }
  }
  if (L.reload.visible) glassButton(ui_, L.reload.c, L.reload.r, false, false, "reload", kAccent, a, 0.6f);
  if (L.interact.visible) {
    float pulse = 0.5f + 0.5f * std::sin(realTime_ * 5.0f);
    ui_.circle(L.interact.c.x, L.interact.c.y, L.interact.r + (5 + 5 * pulse) * S, C(1, 1, 1, 0), 2.0f * S, withAlpha(kAccent, (0.5f - 0.3f * pulse) * a));
    glassButton(ui_, L.interact.c, L.interact.r, in.interactHeld, true, focus_.icon, kAccent, a);
  }
  if (L.enterExit.visible) glassButton(ui_, L.enterExit.c, L.enterExit.r, in.enterExitHeld, true, driving ? "door" : "car", kMint, a);
  glassButton(ui_, L.camera.c, L.camera.r, in.cameraHeld, false, "camera", kAccent, a);
  glassButton(ui_, L.wheel.c, L.wheel.r, in.wheelBtnHeld, wheel_.open, "wheel", kAccent, a);
  ui_.text(false, "ARMAS", L.wheel.c.x, L.wheel.c.y + L.wheel.r + 6 * S, 14 * S, C(1, 1, 1, 0.7f * a), Align::Center);
  ui_.text(false, cam_.mode() == CamMode::TopDown ? "TOP DOWN" : "3ª PESSOA", L.camera.c.x, L.camera.c.y + L.camera.r + 6 * S, 16 * S, C(1, 1, 1, 0.7f * a), Align::Center);
  glassButton(ui_, L.pause.c, L.pause.r, in.pauseHeld, false, "pause", kAccent, a, 0.62f);
}

void Game::drawMinimap() {
  float S = uiScale();
  float a = 1.0f - 0.85f * wheel_.anim;
  float size = 236 * S;
  float x = screenW_ - 64 * S * 2 - 18 * S - size - 8 * S, y = 30 * S;
  // Pause button sits to the right of the map (user layout: map first, then pause).
  ui_.glow(x, y + 6, size, size, 18 * S, 16 * S, C(0, 0, 0, 0.42f * a));
  Vec2 pos = player_.pos;
  if (player_.vehicle >= 0) pos = vehicles_[player_.vehicle].pos;
  float span = player_.vehicle >= 0 ? 110.0f : 84.0f;            // metres across the map widget
  if (player_.indoors) span = 22.0f;
  float k = size / span;                                          // pixels per metre
  float yaw = cam_.yaw();
  float cu = 0.5f + pos.x / (2.0f * mapExtent_), cv = 0.5f + pos.y / (2.0f * mapExtent_);
  if (player_.indoors) {
    ui_.rect(x, y, size, size, C(0.11f, 0.13f, 0.17f, 0.9f * a), 18 * S);
  } else {
    ui_.map(mapTex_, cu, cv, span / (2.0f * mapExtent_), yaw, x, y, size, size, 18 * S, C(1, 1, 1, 0.96f * a));
  }
  ui_.rect(x, y, size, size, 0, 18 * S, 2.5f * S, C(1, 1, 1, 0.55f * a));
  Vec2 c{x + size * 0.5f, y + size * 0.5f};
  Vec2 right{std::cos(yaw), std::sin(yaw)}, fwd{std::sin(yaw), -std::cos(yaw)};
  auto marker = [&](Vec2 world, const char* icon, Color col, float sz, bool clampEdge) {
    Vec2 rel = world - pos;
    Vec2 m{rel.dot(right) * k, -rel.dot(fwd) * k};
    float half = size * 0.5f - sz * 0.6f;
    bool outside = std::fabs(m.x) > half || std::fabs(m.y) > half;
    if (outside) {
      if (!clampEdge) return;
      float sc = half / std::max(std::fabs(m.x), std::fabs(m.y));
      m = m * sc;
      col = withAlpha(col, 0.55f);
    }
    ui_.icon(icon, c.x + m.x, c.y + m.y, sz, col);
  };
  if (!player_.indoors) {
    marker({world_.poiGas.x, world_.poiGas.z}, "fuel", kMint, 30 * S, true);
    for (const ShopDef& sh : world_.shops)
      marker({sh.door.x, sh.door.z}, "cart", kAccent, 26 * S, sh.kind == ShopKind::Mercado);
    marker({world_.poiWorkshop.x, world_.poiWorkshop.z}, "wrench", kSky, 30 * S, true);
    for (const Vehicle& v : vehicles_)
      if (player_.vehicle != v.id && !v.despawn) marker(v.pos, "car", v.police && v.siren ? (std::fmod(realTime_ * 2.6f, 1.0f) < 0.5f ? kRed : kSky) : C(1, 1, 1, 0.95f), 22 * S, false);
    // officers on the radar while wanted (blink red/blue); weapon pickups as small markers
    if (wanted_ > 0)
      for (const Npc& c : npcs_)
        if (c.police && !c.despawn && c.state != NpcState::Dead) marker(c.pos, "dot", std::fmod(realTime_ * 2.6f, 1.0f) < 0.5f ? kRed : kSky, 16 * S, false);
    for (const Pickup& k : pickups_)
      if (k.active) marker({k.pos.x, k.pos.z}, weaponDef(k.weapon).icon, C(1.0f, 0.85f, 0.4f, 0.95f), 20 * S, false);
    if (waypoint_.active) marker({waypoint_.pos.x, waypoint_.pos.z}, "pin", kRed, 34 * S, true);
  }
  // player heading wedge + dot
  float heading = (player_.vehicle >= 0 ? vehicles_[player_.vehicle].yaw : player_.yaw) - yaw;
  ui_.arc(c.x, c.y, 0, 22 * S, heading - 0.55f, heading + 0.55f, C(1, 1, 1, 0.92f * a));
  ui_.circle(c.x, c.y, 7.5f * S, kAccent, 2.0f * S, C(0.05f, 0.05f, 0.08f, 0.9f));
  if (cam_.mode() == CamMode::ThirdPerson || std::fabs(wrapAngle(yaw)) > 0.05f) {
    // compass N marker
    Vec2 n{-fwd.x * 0 + 0, 0};
    (void)n;
    Vec2 up{-std::sin(yaw) * -1.0f, 0};
    (void)up;
    float ang = -yaw;   // north relative to up
    Vec2 np{c.x + std::sin(ang) * (size * 0.5f - 14 * S), c.y - std::cos(ang) * (size * 0.5f - 14 * S)};
    ui_.circle(np.x, np.y, 11 * S, C(0.05f, 0.06f, 0.09f, 0.8f * a));
    ui_.text(true, "N", np.x, np.y - 10 * S, 17 * S, kAccent, Align::Center);
  }
}

void Game::drawBars() {
  float S = uiScale();
  float a = 1.0f - 0.85f * wheel_.anim;
  float x = 40 * S, y = 40 * S;
  float hAlpha = healthShow_ > 0.0f ? 1.0f : 0.0f;
  float hv = clamp(player_.health / 100.0f, 0.0f, 1.0f);
  Color hc = hv > 0.35f ? mixColor(kRed, kMint, 0.0f) : kRed;
  ui_.icon("heart", x + 10 * S, y + 10 * S, 22 * S, withAlpha(kRed, hAlpha * a));
  ui_.rect(x + 28 * S, y + 6 * S, 230 * S, 8 * S, C(1, 1, 1, 0.14f * a * hAlpha), 4 * S);
  ui_.rect(x + 28 * S, y + 6 * S, 230 * S * hv, 8 * S, withAlpha(hc, hAlpha * a), 4 * S);
  if (staminaShow_ > 0.0f) {
    float sa = std::min(1.0f, staminaShow_) * a;
    float sv = clamp(player_.stamina / 100.0f, 0.0f, 1.0f);
    ui_.icon("bolt", x + 10 * S, y + 34 * S, 22 * S, withAlpha(kSky, sa));
    ui_.rect(x + 28 * S, y + 30 * S, 190 * S, 6 * S, C(1, 1, 1, 0.14f * sa), 3 * S);
    ui_.rect(x + 28 * S, y + 30 * S, 190 * S * sv, 6 * S, withAlpha(player_.runBoost > 0 ? kAccent : kSky, sa), 3 * S);
  }
}

void Game::drawMoney() {
  float S = uiScale();
  float a = (1.0f - 0.85f * wheel_.anim) * clamp(moneyShow_ * 2.5f, 0.0f, 1.0f);
  if (a < 0.01f) return;
  float size = 236 * S;
  float mx = screenW_ - 64 * S * 2 - 18 * S - size - 8 * S;
  std::string txt = fmtMoney((int)std::lround(moneyDisplay_));
  float tw = ui_.textWidth(true, txt, 34 * S);
  float w = tw + 78 * S, h = 56 * S;
  float x = mx + size - w, y = 30 * S + size + 14 * S + (1.0f - clamp(moneyShow_ * 2.5f, 0.0f, 1.0f)) * -12.0f * S;
  ui_.glow(x, y + 4, w, h, 18 * S, 12 * S, C(0, 0, 0, 0.35f * a));
  ui_.rect(x, y, w, h, withAlpha(kGlass, a), h / 2, 1.5f * S, C(1, 1, 1, 0.20f * a));
  ui_.icon("coin", x + 30 * S, y + h / 2, 30 * S, withAlpha(kAccent, a));
  ui_.text(true, txt, x + 54 * S, y + 8 * S, 34 * S, C(1, 1, 1, a), Align::Left);
}

void Game::drawFuelGauge() {
  if (player_.vehicle < 0) return;
  const Vehicle& v = vehicles_[player_.vehicle];
  const VehicleDef& d = vehicleDef(v.model);
  float S = uiScale();
  float a = 1.0f - 0.85f * wheel_.anim;
  float cx = screenW_ * 0.5f, y = screenH_ - 150 * S;
  int kmh = (int)std::lround(std::fabs(v.speed) * 3.6f);
  ui_.glow(cx - 230 * S, y, 460 * S, 112 * S, 28 * S, 14 * S, C(0, 0, 0, 0.30f * a));
  ui_.rect(cx - 230 * S, y, 460 * S, 112 * S, withAlpha(kGlass, a), 28 * S, 1.5f * S, C(1, 1, 1, 0.16f * a));
  ui_.text(true, std::to_string(kmh), cx - 130 * S, y + 6 * S, 66 * S, C(1, 1, 1, a), Align::Center);
  ui_.text(false, "km/h", cx - 130 * S, y + 72 * S, 22 * S, withAlpha(kMuted, a), Align::Center);
  float fv = clamp(v.fuel / d.fuelCap, 0.0f, 1.0f);
  bool low = fv < 0.15f;
  float blink = low ? (0.55f + 0.45f * std::sin(realTime_ * 7.0f)) : 1.0f;
  Color fc = fv > 0.35f ? kMint : (fv > 0.15f ? kAccent : kRed);
  float bx = cx - 40 * S, bw = 230 * S;
  ui_.icon("fuel", bx + 12 * S, y + 28 * S, 26 * S, withAlpha(fc, a * blink));
  ui_.rect(bx + 34 * S, y + 24 * S, bw - 34 * S, 9 * S, C(1, 1, 1, 0.14f * a), 4.5f * S);
  ui_.rect(bx + 34 * S, y + 24 * S, (bw - 34 * S) * fv, 9 * S, withAlpha(fc, a * blink), 4.5f * S);
  ui_.text(false, fmtFloat(v.fuel, 1) + " L", bx + bw, y + 36 * S, 20 * S, withAlpha(kMuted, a), Align::Right);
  float hv = clamp(v.health / 100.0f, 0.0f, 1.0f);
  Color hc = hv > 0.5f ? kMint : (hv > 0.25f ? kAccent : kRed);
  ui_.icon("car", bx + 12 * S, y + 72 * S, 24 * S, withAlpha(hc, a));
  ui_.rect(bx + 34 * S, y + 68 * S, bw - 34 * S, 9 * S, C(1, 1, 1, 0.14f * a), 4.5f * S);
  ui_.rect(bx + 34 * S, y + 68 * S, (bw - 34 * S) * hv, 9 * S, withAlpha(hc, a), 4.5f * S);
  ui_.text(false, fmtFloat(v.health, 0) + "%", bx + bw, y + 80 * S, 20 * S, withAlpha(kMuted, a), Align::Right);
  if (low && fv > 0.0f) ui_.text(true, "COMBUSTÍVEL BAIXO", cx, y - 40 * S, 28 * S, withAlpha(kRed, blink * a), Align::Center, C(0, 0, 0, 0.6f), 0.12f);
  if (v.fuel <= 0.0f) ui_.text(true, "SEM COMBUSTÍVEL", cx, y - 40 * S, 30 * S, withAlpha(kRed, a), Align::Center, C(0, 0, 0, 0.6f), 0.12f);
}

void Game::drawPrompt() {
  if(realTime_<subtitleUntil_&&(subtitleSound_?settings_.soundCaptions:settings_.subtitles)){promptAnim_=0;return;}
  if (!focusValid_ || panel_.open || wheel_.open || menu_ != MenuState::None || fadeAlpha_ > 0.3f) { promptAnim_ = std::max(0.0f, promptAnim_ - 0.1f); if (promptAnim_ <= 0) return; }
  else promptAnim_ = std::min(1.0f, promptAnim_ + 0.14f);
  float S = uiScale();
  float a = promptAnim_ * (1.0f - wheel_.anim);
  const Interactable& it = focus_;
  std::string label = it.label, sub = it.sub;
  float tw = std::max(ui_.textWidth(true, label, 32 * S), ui_.textWidth(false, sub, 23 * S));
  bool hasArt = !it.art.empty();
  float h = 84 * S, w = tw + (hasArt ? 190 : 150) * S;
  float cx = screenW_ * 0.5f - 60 * S, y = screenH_ - (player_.vehicle >= 0 ? 300.0f : 175.0f) * S + (1.0f - promptAnim_) * 20 * S;
  float x = cx - w / 2;
  ui_.glow(x, y + 4, w, h, h / 2, 14 * S, C(0, 0, 0, 0.33f * a));
  ui_.rect(x, y, w, h, withAlpha(kGlass, a), h / 2, 1.5f * S, C(1, 1, 1, 0.20f * a));
  ui_.circle(x + h / 2, y + h / 2, 30 * S, withAlpha(it.enabled ? kAccent : kMuted, 0.22f * a), 2 * S, withAlpha(it.enabled ? kAccent : kMuted, 0.85f * a));
  ui_.icon(it.icon, x + h / 2, y + h / 2, 34 * S, withAlpha(it.enabled ? kAccent : kMuted, a));
  float tx = x + h + 8 * S;
  if (hasArt) { ui_.art(it.art.c_str(), tx - 4 * S, y + 12 * S, 60 * S, 60 * S, withAlpha(kWhite, a), 12 * S); tx += 66 * S; }
  ui_.text(true, label, tx, y + 10 * S, 32 * S, C(1, 1, 1, a), Align::Left);
  ui_.text(false, sub, tx, y + 49 * S, 23 * S, withAlpha(kMuted, a), Align::Left);
}

void Game::drawToasts(float dt) {
  float S = uiScale();
  for (Toast& t : toasts_) t.t += dt;
  while (!toasts_.empty() && toasts_.front().t > toasts_.front().dur + 0.5f) toasts_.pop_front();
  float a0 = 1.0f - 0.85f * wheel_.anim;
  float x = 40 * S, y = 104 * S;
  for (size_t i=toasts_.size()>2?toasts_.size()-2:0;i<toasts_.size();++i) {
    const Toast& t=toasts_[i];
    float in = settings_.reducedMotion?1.0f:clamp(t.t / 0.25f, 0.0f, 1.0f), out = clamp((t.dur + 0.4f - t.t) / 0.4f, 0.0f, 1.0f);
    float a = std::min(in, out) * a0;
    if (a <= 0.01f) continue;
    float tw = ui_.textWidth(false, t.text, 26 * S);
    float h = 46 * S, w = tw + (t.icon ? 82 : 44) * S;
    float xx = x - (1.0f - smoothstep(in)) * 30 * S;
    ui_.glow(xx, y + 3, w, h, h / 2, 10 * S, C(0, 0, 0, 0.28f * a));
    ui_.rect(xx, y, w, h, withAlpha(kGlass, a), h / 2, 1.2f * S, C(1, 1, 1, 0.16f * a));
    float tx = xx + 22 * S;
    if (t.icon) { ui_.icon(t.icon, xx + 28 * S, y + h / 2, 26 * S, withAlpha(kAccent, a)); tx = xx + 52 * S; }
    ui_.text(false, t.text, tx, y + 8 * S, 26 * S, withAlpha(t.color, a), Align::Left);
    y += h + 8 * S;
  }
}

static float dist2(Vec2 a, Vec2 b) { return (a - b).length(); }

void Game::drawHud(float dt, const InputFrame& in) {
  float S = uiScale();
  bool menuOpen = menu_ != MenuState::None;
  if (!menuOpen) {
    drawBars();
    drawMinimap();
    drawMoney();
    drawFuelGauge();
    drawPrompt();
    drawToasts(dt);
    drawTouchControls(in);
    // wanted level: three stars under the minimap; blinking while an officer sees the player, dim while searching
    if (wanted_ > 0 || wantedHeat_ > 0.01f) {
      float a = 1.0f - 0.85f * wheel_.anim;
      float sz = 34 * S;
      float x0 = screenW_ - 64 * S * 2 - 18 * S - 8 * S - sz * 3.4f, y0 = 30 * S + 236 * S + 14 * S;
      bool seenNow = sinceSeen_ < 0.5f;
      bool blink = seenNow && std::fmod(realTime_ * 3.0f, 1.0f) < 0.5f;
      for (int i = 0; i < 3; ++i) {
        bool on = i < wanted_;
        Color col = on ? (blink ? kWhite : (seenNow ? kAccent : withAlpha(kAccent, 0.55f))) : C(1, 1, 1, 0.18f);
        ui_.icon("star", x0 + sz * 0.5f + i * sz * 1.15f, y0 + sz * 0.5f, sz, withAlpha(col, ((col >> 24) / 255.0f) * a));
      }
      if (wanted_ > 0 && !seenNow && sinceSeen_ > 2.0f)
        ui_.text(false, "Polícia procurando...", x0 + sz * 1.7f, y0 + sz + 6 * S, 16 * S, C(1, 1, 1, 0.7f * a), Align::Center);
    }
    // waypoint pill
    if (waypoint_.active) {
      float d = dist2(player_.pos, {waypoint_.pos.x, waypoint_.pos.z});
      if (d < 7.0f && !player_.indoors) { waypoint_.active = false; toast("Você chegou: " + waypoint_.name, "pin"); }
      else {
        std::string t = waypoint_.name + " • " + std::to_string((int)d) + " m";
        float tw = ui_.textWidth(false, t, 24 * S);
        float w = tw + 70 * S, h = 44 * S, x = screenW_ * 0.5f - w / 2 - 120 * S, y = 32 * S;
        float a = 1.0f - 0.85f * wheel_.anim;
        ui_.rect(x, y, w, h, withAlpha(kGlass, a), h / 2, 1.2f * S, C(1, 1, 1, 0.16f * a));
        ui_.icon("pin", x + 26 * S, y + h / 2, 26 * S, withAlpha(kRed, a));
        ui_.text(false, t, x + 48 * S, y + 8 * S, 24 * S, C(1, 1, 1, a), Align::Left);
      }
    }
    // fuelling progress
    if (fueling_.active) {
      float prog = 1.0f - fueling_.litersLeft / std::max(0.01f, fueling_.total);
      float w = 420 * S, h = 52 * S, x = screenW_ * 0.5f - w / 2 - 60 * S, y = screenH_ * 0.5f - 160 * S;
      ui_.rect(x, y, w, h, kGlass, h / 2, 1.2f * S, C(1, 1, 1, 0.2f));
      ui_.icon("fuel", x + 30 * S, y + h / 2, 28 * S, kMint);
      ui_.rect(x + 56 * S, y + 21 * S, w - 80 * S, 10 * S, C(1, 1, 1, 0.14f), 5 * S);
      ui_.rect(x + 56 * S, y + 21 * S, (w - 80 * S) * prog, 10 * S, kMint, 5 * S);
    }
    // speech bubbles
    for (const Npc& n : npcs_) {
      if (n.bubbleTimer <= 0 || n.bubble.empty() || n.interior != player_.indoors) continue;
      Vec2 sp; bool vis;
      projectToScreen({n.pos.x, n.y + 1.95f, n.pos.y}, sp, vis);
      if (!vis || sp.x < 0 || sp.x > screenW_ || sp.y < 0 || sp.y > screenH_) continue;
      float a = clamp(n.bubbleTimer, 0.0f, 0.4f) / 0.4f;
      float tw = ui_.textWidth(true, n.bubble, 24 * S);
      float w = tw + 30 * S, h = 40 * S;
      ui_.rect(sp.x - w / 2, sp.y - h - 8 * S, w, h, C(1, 1, 1, 0.92f * a), h / 2);
      ui_.text(true, n.bubble, sp.x, sp.y - h - 2 * S, 24 * S, C(0.08f, 0.09f, 0.12f, a), Align::Center);
    }
  }
}

// ------------------------------------------------------------------------------------------------ panel
void Game::drawPanel(float dt) {
  panel_.anim = settings_.reducedMotion?1:panel_.anim+(1.0f-panel_.anim)*expDecay(14.0f,dt);
  float S = std::min(uiScale(),screenH_/1000.0f);
  float a = panel_.anim;
  uiRects_.clear();
  ui_.rect(0, 0, screenW_, screenH_, C(0, 0, 0, 0.30f * a));
  float cardW = std::min(1040.0f * S, screenW_ - 60.0f * S);
  float pad = 34 * S;
  bool portrait = !panel_.portrait.empty();
  float portSize = portrait ? 188 * S : 0;
  float textX = pad + (portrait ? portSize + 28 * S : 0);
  float textW = cardW - textX - pad;
  auto lines = wrapText(ui_, panel_.text, textW, 28 * S, false);
  float titleH = 52 * S, lineH = 36 * S;
  float headerH = std::max(portSize, titleH + lines.size() * lineH + 8 * S);
  float gaugeH = (panel_.vehicleFuel >= 0 || panel_.vehicleHealth >= 0 || !panel_.footer.empty()) ? 54 * S : 0;
  size_t n = panel_.options.size();
  float rowH = n > 6 ? 70 * S : 90 * S, gap = 10 * S;
  float total = pad + headerH + 18 * S + gaugeH + n * (rowH + gap) + pad - gap;
  float maxH = screenH_ - 50 * S;
  if (total > maxH) { rowH = std::max(50 * S, rowH - (total - maxH) / std::max<size_t>(1, n)); total = pad + headerH + 18 * S + gaugeH + n * (rowH + gap) + pad - gap; }
  float x = (screenW_ - cardW) / 2, y = screenH_ - total - 28 * S + (1.0f - a) * 90 * S;
  ui_.glow(x, y + 8, cardW, total, 30 * S, 26 * S, C(0, 0, 0, 0.5f * a));
  ui_.rect(x, y, cardW, total, C(0.045f, 0.055f, 0.085f, 0.88f * a), 30 * S, 1.6f * S, C(1, 1, 1, 0.16f * a));
  if (portrait) {
    std::string key = "portrait_" + panel_.portrait;
    ui_.rect(x + pad - 4 * S, y + pad - 4 * S, portSize + 8 * S, portSize + 8 * S, C(1, 1, 1, 0.16f * a), 26 * S);
    ui_.art(key.c_str(), x + pad, y + pad, portSize, portSize, C(1, 1, 1, a), 22 * S);
  }
  ui_.text(true, panel_.title, x + textX, y + pad - 4 * S, 44 * S, C(1, 1, 1, a), Align::Left);
  float ty = y + pad + titleH - 2 * S;
  for (const std::string& l : lines) { ui_.text(false, l, x + textX, ty, 28 * S, C(1, 1, 1, 0.82f * a), Align::Left); ty += lineH; }
  float oy = y + pad + headerH + 18 * S;
  if (gaugeH > 0) {
    float gx = x + pad, gw = cardW - pad * 2;
    ui_.rect(gx, oy, gw, gaugeH - 8 * S, C(1, 1, 1, 0.07f * a), 16 * S);
    float pad2 = 20 * S;
    if (!panel_.footer.empty()) ui_.text(false, panel_.footer, gx + pad2, oy + 10 * S, 25 * S, C(1, 1, 1, 0.85f * a), Align::Left);
    float bw = 260 * S, bx = gx + gw - bw - pad2;
    if (panel_.vehicleFuel >= 0 && panel_.vehicleCap > 0) {
      float live = panel_.vehicleFuel;
      if (fueling_.active && fueling_.vehicle >= 0) live = vehicles_[fueling_.vehicle].fuel;
      ui_.icon("fuel", bx - 24 * S, oy + 20 * S, 28 * S, withAlpha(kMint, a));
      ui_.rect(bx, oy + 16 * S, bw, 10 * S, C(1, 1, 1, 0.14f * a), 5 * S);
      ui_.rect(bx, oy + 16 * S, bw * clamp(live / panel_.vehicleCap, 0.0f, 1.0f), 10 * S, withAlpha(kMint, a), 5 * S);
    }
    if (panel_.vehicleHealth >= 0) {
      ui_.icon("wrench", bx - 24 * S, oy + 20 * S, 28 * S, withAlpha(kSky, a));
      ui_.rect(bx, oy + 16 * S, bw, 10 * S, C(1, 1, 1, 0.14f * a), 5 * S);
      float hv = clamp(panel_.vehicleHealth / 100.0f, 0.0f, 1.0f);
      ui_.rect(bx, oy + 16 * S, bw * hv, 10 * S, withAlpha(hv > 0.5f ? kMint : (hv > 0.25f ? kAccent : kRed), a), 5 * S);
    }
    oy += gaugeH;
  }
  for (size_t i = 0; i < n; ++i) {
    const PanelOption& o = panel_.options[i];
    float rx = x + pad, rw = cardW - pad * 2;
    bool pressed = pressedUi_ == (int)i;
    float oa = o.enabled ? 1.0f : 0.45f;
    Color fill = pressed ? C(1, 1, 1, 0.22f * a) : C(1, 1, 1, 0.095f * a);
    ui_.rect(rx, oy, rw, rowH, fill, 22 * S, 1.2f * S, C(1, 1, 1, 0.12f * a * oa));
    float ix = rx + 22 * S;
    float iconBox = rowH - 20 * S;
    if (!o.art.empty()) { ui_.art(o.art.c_str(), rx + 10 * S, oy + 10 * S, iconBox, iconBox, C(1, 1, 1, a * oa), 14 * S); ix = rx + iconBox + 26 * S; }
    else if (o.icon) { ui_.icon(o.icon, rx + 20 * S + iconBox * 0.4f, oy + rowH / 2, iconBox * 0.62f, withAlpha(kAccent, a * oa)); ix = rx + iconBox + 22 * S; }
    float ls = rowH > 80 * S ? 34.0f : 30.0f;
    if (o.sub.empty()) ui_.text(true, o.label, ix, oy + rowH / 2 - ls * 0.62f * S, ls * S, C(1, 1, 1, a * oa), Align::Left);
    else {
      ui_.text(true, o.label, ix, oy + 8 * S, ls * S, C(1, 1, 1, a * oa), Align::Left);
      ui_.text(false, o.sub, ix, oy + rowH - 36 * S, 23 * S, withAlpha(kMuted, a * oa), Align::Left);
    }
    uiRects_.push_back({Vec4(rx, oy, rw, rowH), (int)i});
    oy += rowH + gap;
  }
}

void Game::selectPanelOption(int idx) {
  if (!panel_.open || idx < 0 || idx >= (int)panel_.options.size()) return;
  PanelOption o = panel_.options[idx];
  if (!o.enabled) { toast("Indisponível", "lock", rgba(1.0f, 0.6f, 0.5f)); return; }
  if (o.closes) closePanel();
  if (o.action) o.action();
}

// ------------------------------------------------------------------------------------------------ wheel
void Game::drawWheel(float dt) {
  (void)dt;
  if (wheel_.anim < 0.01f) return;
  float S = uiScale();
  float a = wheel_.anim;
  float R = std::min(screenW_, screenH_) * 0.40f * (0.88f + 0.12f * a);
  Vec2 c{screenW_ * 0.5f, screenH_ * 0.5f};
  ui_.rect(0, 0, screenW_, screenH_, C(0.0f, 0.0f, 0.02f, 0.28f * a));
  const auto& slots = wheel_.slots[wheel_.category];
  const bool weapons = wheel_.category == 0;
  const int nSec = weapons ? kWeaponCount : 8;
  const float gap = weapons ? 0.035f : 0.045f;
  // outer sectors
  for (int i = 0; i < nSec; ++i) {
    float a0 = i * kTau / nSec - kTau / (2 * nSec) + gap, a1 = (i + 1) * kTau / nSec - kTau / (2 * nSec) - gap;
    bool has = i < (int)slots.size();
    bool hov = wheel_.hovered == i;
    bool current = weapons && has && slots[i] == player_.weapon;
    Color col = hov ? withAlpha(kAccent, 0.88f * a) : C(0.05f, 0.06f, 0.09f, (has ? 0.62f : 0.30f) * a);
    ui_.arc(c.x, c.y, R * 0.52f, R, a0, a1, col);
    if (current && !hov) ui_.arc(c.x, c.y, R * 0.985f, R * 1.02f, a0, a1, withAlpha(kAccent, 0.9f * a));
    float am = (a0 + a1) * 0.5f;
    Vec2 p{c.x + std::sin(am) * R * 0.76f, c.y - std::cos(am) * R * 0.76f};
    Color ink = hov ? C(0.1f, 0.1f, 0.12f, a) : C(1, 1, 1, a);
    if (has && weapons) {
      const WeaponDef& w = weaponDef(slots[i]);
      ui_.icon(w.icon, p.x, p.y - 6 * S, 64 * S, ink);
      if (w.magazine > 0)
        ui_.text(true, std::to_string(player_.mag[w.id]) + "/" + std::to_string(player_.reserve[w.id]), p.x, p.y + 26 * S, 18 * S, ink, Align::Center);
    } else if (has) {
      const ItemDef& d = itemDef(slots[i]);
      if (d.art) ui_.art(d.art, p.x - 40 * S, p.y - 40 * S, 80 * S, 80 * S, C(1, 1, 1, a * (hov ? 1.0f : 0.92f)), 16 * S);
      else ui_.icon(d.icon, p.x, p.y, 62 * S, ink);
      std::string cnt = "x" + std::to_string(inventory_[slots[i]]);
      ui_.text(true, cnt, p.x + 30 * S, p.y + 18 * S, 24 * S, ink, Align::Center, C(0, 0, 0, 0.7f), 0.1f);
    } else {
      ui_.circle(p.x, p.y, 7 * S, C(1, 1, 1, 0.18f * a));
    }
  }
  // inner category ring: top half = weapons, bottom half = items
  for (int cat = 0; cat < 2; ++cat) {
    float a0 = cat == 0 ? -kPi / 2 + 0.06f : kPi / 2 + 0.06f, a1 = cat == 0 ? kPi / 2 - 0.06f : 3 * kPi / 2 - 0.06f;
    bool sel = wheel_.category == cat;
    bool hov = wheel_.hoveredCategory == cat;
    Color col = sel ? withAlpha(kAccent, (hov ? 0.9f : 0.62f) * a) : C(0.05f, 0.06f, 0.09f, 0.62f * a);
    ui_.arc(c.x, c.y, R * 0.16f, R * 0.44f, a0, a1, col);
    float am = cat == 0 ? 0.0f : kPi;
    Vec2 p{c.x + std::sin(am) * R * 0.30f, c.y - std::cos(am) * R * 0.30f};
    ui_.text(true, cat == 0 ? "ARMAS" : "ITENS", p.x, p.y - 12 * S, 22 * S, sel ? C(0.08f, 0.08f, 0.1f, a) : C(1, 1, 1, 0.85f * a), Align::Center);
  }
  // centre info: name, type and ammo of the hovered (or current) weapon / item
  int show = wheel_.hovered >= 0 && wheel_.hovered < (int)slots.size() ? slots[wheel_.hovered] : (weapons ? player_.weapon : -1);
  if (weapons && show >= 0) {
    const WeaponDef& w = weaponDef(show);
    ui_.text(true, w.name, c.x, c.y - 30 * S, 26 * S, C(1, 1, 1, a), Align::Center, C(0, 0, 0, 0.6f), 0.1f);
    ui_.text(false, w.typeLabel, c.x, c.y + 2 * S, 18 * S, C(1, 1, 1, 0.65f * a), Align::Center);
    if (w.magazine > 0)
      ui_.text(false, "Munição " + std::to_string(player_.mag[w.id]) + " + " + std::to_string(player_.reserve[w.id]), c.x, c.y + 26 * S, 18 * S,
               withAlpha(kAccent, a), Align::Center);
    else if (show == player_.weapon) ui_.text(false, "Equipada", c.x, c.y + 26 * S, 18 * S, withAlpha(kAccent, a), Align::Center);
  } else if (!weapons && show >= 0) {
    ui_.text(true, itemDef(show).name, c.x, c.y - 24 * S, 28 * S, C(1, 1, 1, a), Align::Center, C(0, 0, 0, 0.6f), 0.1f);
  } else {
    ui_.text(false, weapons ? "Armas" : "Itens", c.x, c.y - 14 * S, 22 * S, C(1, 1, 1, 0.55f * a), Align::Center);
  }
  if (slots.empty()) ui_.text(false, weapons ? "Sem armas" : "Nenhum item — compre no mercado", c.x, c.y + R * 1.06f, 24 * S, C(1, 1, 1, 0.65f * a), Align::Center);
  else if (!weapons && wheel_.hovered >= 0) ui_.text(false, itemDef(slots[wheel_.hovered]).desc, c.x, c.y + R * 1.06f, 24 * S, C(1, 1, 1, 0.8f * a), Align::Center);
  // finger marker
  Vec2 f = wheel_.finger - c;
  float fl = f.length();
  if (fl > R * 1.1f) f = f * (R * 1.1f / fl);
  ui_.circle(c.x + f.x, c.y + f.y, 14 * S, C(1, 1, 1, 0.85f * a), 2 * S, withAlpha(kAccent, a));
  ui_.rect(c.x - 1.5f * S, c.y - 1.5f * S, 3 * S, 3 * S, C(1, 1, 1, 0.5f * a), 1.5f * S);
}

// ------------------------------------------------------------------------------------------------ menus
void Game::openSettingsMenu() { settingsBack_=menu_; menu_=MenuState::Settings; }

void Game::drawFullMap() {
  float S=uiScale(), size=std::min(screenH_-180*S,screenW_-460*S);
  float x=60*S,y=110*S; float span=2*mapExtent_/mapZoom_,k=size/span;
  ui_.map(mapTex_,0.5f+mapCenter_.x/(2*mapExtent_),0.5f+mapCenter_.y/(2*mapExtent_),1/mapZoom_,0,x,y,size,size,8*S,kWhite);
  uiRects_.push_back({Vec4(x,y,size,size),900});
  auto marker=[&](Vec2 p,const char* icon,Color color,const std::string& label){
    Vec2 m=(p-mapCenter_)*k+Vec2{x+size/2,y+size/2};
    if(m.x<x+18*S||m.x>x+size-18*S||m.y<y+18*S||m.y>y+size-18*S)return;
    ui_.icon(icon,m.x,m.y,26*S,color);
    if(mapZoom_>1.8f&&!label.empty())ui_.text(false,label,m.x+17*S,m.y-10*S,18*S,kWhite);
  };
  for(const auto& sh:world_.shops)marker({sh.door.x,sh.door.z},"cart",kAccent,sh.name);
  marker({world_.poiGas.x,world_.poiGas.z},"fuel",kMint,"Posto");
  marker({world_.poiWorkshop.x,world_.poiWorkshop.z},"wrench",kSky,"Oficina");
  if(world_.coastSide>=0)marker({world_.poiBeach.x,world_.poiBeach.z},"pin",kAccent,"Praia");
  for(const auto& v:vehicles_)if(!v.despawn)marker(v.pos,"car",v.police?kSky:kMuted,"");
  if(wanted_>0)for(const auto& n:npcs_)if(n.police&&!n.despawn&&n.state!=NpcState::Dead)marker(n.pos,"dot",kRed,"");
  Vec2 p=player_.pos;
  if(player_.indoors){int index=world_.interiorAt(p.x,p.y);if(index>=0){int shop=world_.interiors[index].shop;if(shop>=0&&shop<(int)world_.shops.size())p={world_.shops[shop].door.x,world_.shops[shop].door.z};}}
  marker(p,"pin",kWhite,"Você");
  ui_.text(false,"N ↑  •  Arraste para explorar",x,y+size+16*S,22*S,kMuted);
  float tx=x+size+40*S;
  ui_.text(true,world_.cityName,tx,y,32*S,kWhite);
  ui_.text(false,"Seed "+std::to_string(worldSeed_),tx,y+50*S,22*S,kMuted);
  auto b=[&](float yy,const std::string& text,int id){ui_.rect(tx,yy,270*S,54*S,C(1,1,1,0.09f),6*S);ui_.text(false,text,tx+20*S,yy+12*S,24*S,kWhite);uiRects_.push_back({Vec4(tx,yy,270*S,54*S),id});};
  b(y+110*S,"+ Ampliar",901);b(y+178*S,"- Reduzir",902);b(y+246*S,"Centralizar em você",903);b(y+340*S,"Voltar",304);
}

void Game::drawMenus(float dt) {
  if(menu_==MenuState::None)return;
  if(menuVisual_!=menu_){menuVisual_=menu_;menuAnim_=0;menuPage_=0;pressedUi_=-1;}
  menuAnim_=settings_.reducedMotion?1.0f:std::min(1.0f,menuAnim_+dt*5.5f);
  float S=std::min(uiScale(),std::min(screenH_/880.0f,screenW_/1450.0f));
  float opacity=settings_.highContrast?0.99f:0.92f;
  uiRects_.clear();
  if(menu_==MenuState::Controls){drawControlEditor();return;}
  ui_.gradient(0,0,screenW_,screenH_,C(0.018f,0.022f,0.025f,opacity),C(0.07f,0.075f,0.08f,opacity-0.06f));
  if(menu_==MenuState::Map){drawFullMap();return;}
  float left=48*S,split=screenW_*0.33f,x=split+28*S,w=screenW_-x-48*S,y=72*S;
  ui_.rect(split,52*S,1.5f*S,screenH_-104*S,C(1,1,1,0.12f));
  ui_.text(false,"BAIRRO  /  CIDADE VIVA",left,35*S,18*S,kMuted);
  auto text=[&](const std::string& t,float yy,float size=24){for(const auto& line:wrapText(ui_,t,split-left-30*S,size*S,false)){ui_.text(false,line,left,yy,size*S,kMuted);yy+=size*1.35f*S;}};
  struct Row{std::string label,sub;int id;bool enabled;};std::vector<Row> rows;
  auto button=[&](const std::string& label,int id,const std::string& sub="",bool enabled=true){rows.push_back({label,sub,id,enabled});};
  auto title=[&](const std::string& t){ui_.text(true,t,left,105*S,40*S,kWhite);};
  switch(menu_) {
    case MenuState::Main:
      title("BAIRRO");text("Uma cidade. A sua história.",172*S,27);
      text("Explore, encontre pessoas e construa sua próxima partida.",260*S);
      button("CONTINUAR",400,"Retomar o último save",recentSlot()>=0);button("NOVO JOGO",401,"Escolher um slot e uma nova cidade");
      button("CARREGAR JOGO",402,"Seus quatro slots de partida");button("CONFIGURAÇÕES",202);button("SAIR",203);break;
    case MenuState::Slots:{
      title(selectingNew_?"NOVO JOGO":"SEUS SAVES");text("Escolha um slot. A cidade pertence à seed da partida.",180*S);
      for(int i=0;i<4;++i){auto s=inspectSlot(i);std::string detail="Slot livre";
        if(s.exists&&!s.valid)detail="Arquivo inválido • sem backup recuperável";
        else if(s.valid)detail=fmtMoney(s.money)+" • "+std::to_string((int)s.playtime/60)+" min • "+s.location+" • seed "+std::to_string(s.seed)+" • "+s.date+(s.recovered?" • backup disponível":"");
        button("SAVE 0"+std::to_string(i+1)+(i==selectedSlot_?"  [selecionado]":""),410+i,detail);}
      auto s=inspectSlot(selectedSlot_);button(selectingNew_?(s.exists?"SUBSTITUIR SLOT":"CRIAR NOVO SAVE"):"CARREGAR SLOT",420,"",selectingNew_||s.valid);
      if(s.exists)button("EXCLUIR SLOT",421,"",!sessionActive_||selectedSlot_!=activeSlot_);
      button("Voltar",304);break;
    }
    case MenuState::Confirm:
      title(confirmDelete_?"EXCLUIR SAVE?":"SUBSTITUIR?");text("SAVE 0"+std::to_string(selectedSlot_+1)+" será removido. Confira o slot antes de confirmar.",180*S);
      button("Confirmar",430);button("Cancelar",431);break;
    case MenuState::Pause:
      title("PAUSADO");text(world_.cityName,180*S,27);text(fmtMoney(moneyCents_)+" • "+std::to_string((int)time_/60)+" min",235*S);
      text("SAVE 0"+std::to_string(activeSlot_+1)+"\nSeed "+std::to_string(worldSeed_),300*S);
      button("CONTINUAR",200);button("MAPA",204);button("INVENTÁRIO",205);button("CÓDIGOS",206);button("CONFIGURAÇÕES",202);button("SALVAR PARTIDA",201);button("MENU PRINCIPAL",207);break;
    case MenuState::Inventory:
      title("INVENTÁRIO");text("Escolha um consumível para usar ou uma arma para equipar.",180*S);
      button(inventoryTab_?"ARMAS — ver consumíveis":"CONSUMÍVEIS — ver armas",520);
      if(inventoryTab_){for(int i=0;i<kWeaponCount;++i)if(player_.owned[i])button(std::string(weaponDef(i).name)+(i==player_.weapon?" • equipada":""),530+i,std::to_string(player_.mag[i])+" / "+std::to_string(player_.reserve[i])+" munições");}
      else {for(int i=1;i<kItemCount;++i)if(inventory_[i]>0)button(std::string(itemDef(i).name)+" ×"+std::to_string(inventory_[i]),500+i,"Usar consumível");
        if(rows.size()==1)text("Sem consumíveis. Visite uma loja.",310*S);}
      button("Voltar",304);break;
    case MenuState::Codes:{
      title("CÓDIGOS");text("Ações imediatas para diversão e testes.",180*S);
      static const char* codes[]={"RECUPERAR VIDA","DAR ARMAS","MUNIÇÃO","DINHEIRO DE TESTE","REMOVER PROCURADO","ADICIONAR PROCURADO","REPARAR VEÍCULO","ENCHER TANQUE"};
      for(int i=0;i<8;++i)button(codes[i],600+i);
      button("Voltar",304);break;
    }
    case MenuState::Settings:{
      title("AJUSTES");text("Personalize a apresentação e os controles. Alterações são salvas automaticamente.",180*S);
      static const char* tabs[]={"Gráficos","Áudio","Controles","Jogo","Acesso"};float tw=w/5;
      for(int i=0;i<5;++i){ui_.rect(x+tw*i,y,tw-5*S,52*S,C(1,1,1,settingsTab_==i?0.18f:0.045f),5*S);ui_.text(false,tabs[i],x+tw*(i+0.5f),y+16*S,20*S,kWhite,Align::Center);uiRects_.push_back({Vec4(x+tw*i,y,tw-5*S,52*S),700+i});}y+=75*S;
      if(settingsTab_==0){button(std::string("Qualidade: ")+preset().name,303);button("Resolução: "+std::to_string((int)(settings_.resolution*100))+"%",710);
        button(std::string("Sombras: ")+(settings_.shadows?"ativadas":"desativadas"),301);button("Vegetação: "+std::to_string((int)(settings_.vegetation*100))+"%",711);
        button("Distância: "+std::to_string((int)(settings_.renderDistance*100))+"%",712);button(std::string("Efeitos: ")+(settings_.effects?"ativados":"desativados"),713);
        button(std::string("Resolução dinâmica: ")+(settings_.dynamicRes?"ativada":"desativada"),305);button("Limite de FPS: "+std::to_string(settings_.fpsLimit),718);
        const char* modes[]={"desligados","céu","costa / cena visível"};button(std::string("Reflexos: ")+modes[settings_.reflections],719);button(std::string("Oclusão ambiente: ")+(settings_.ambientOcclusion?"ativada":"desativada"),730);
      }else if(settingsTab_==1){button("Volume: "+std::to_string((int)(settings_.volume*100))+"%",714);button(std::string("Legendas de sons: ")+(settings_.soundCaptions?"sim":"não"),724);}
      else if(settingsTab_==2){button("Sensibilidade: "+fmtFloat(settings_.sensitivity,1)+"x",715);button(std::string("Inverter Y: ")+(settings_.invertY?"sim":"não"),300);
        button("Tamanho dos controles: "+std::to_string((int)(settings_.controlScale*100))+"%",720);button("Opacidade: "+std::to_string((int)(settings_.controlOpacity*100))+"%",721);
        button(std::string("Joystick: ")+(settings_.fixedJoystick?"fixo":"flutuante"),726);button(std::string("Layout canhoto: ")+(settings_.leftHanded?"sim":"não"),727);button("POSICIONAR CONTROLES",728);}
      else if(settingsTab_==3){button(std::string("Ciclo dia / noite: ")+(settings_.dayCycle?"ativo":"congelado"),716);const char* weather[]={"automático","limpo","chuva"};button(std::string("Clima: ")+weather[settings_.weatherMode],729);}
      else {button("Escala da interface: "+std::to_string((int)(settings_.hudScale*100))+"%",717);button(std::string("Alto contraste: ")+(settings_.highContrast?"sim":"não"),722);
        button(std::string("Legendas de diálogo: ")+(settings_.subtitles?"sim":"não"),723);button(std::string("Movimento reduzido: ")+(settings_.reducedMotion?"sim":"não"),725);button(std::string("Mostrar FPS: ")+(settings_.showFps?"sim":"não"),302);}
      button("Voltar",304);break;
    }
    default:break;
  }
  bool back=!rows.empty()&&rows.back().id==304;Row footer;if(back){footer=rows.back();rows.pop_back();}
  float bottom=screenH_-125*S,available=bottom-y;bool detailed=std::any_of(rows.begin(),rows.end(),[](const Row&r){return !r.sub.empty();});
  float rowH=(detailed?104:66)*S;int capacity=std::max(1,(int)(available/(rowH+10*S)));
  menuPages_=std::max(1,((int)rows.size()+capacity-1)/capacity);menuPage_=clamp(menuPage_,0,menuPages_-1);
  float slide=settings_.reducedMotion?0:18*S*(1-menuAnim_);
  auto renderRow=[&](const Row& row,float yy,float height){
    Color fill=pressedUi_==row.id?C(1,1,1,0.2f):C(1,1,1,0.06f+0.025f*menuAnim_);
    if(row.id>=410&&row.id<414&&row.id-410==selectedSlot_)fill=C(0.25f,0.31f,0.34f,0.65f);
    ui_.rect(x+slide,yy,w,height,fill,7*S,1*S,C(1,1,1,0.1f));
    ui_.text(false,row.label,x+24*S+slide,yy+17*S,27*S,row.enabled?kWhite:kMuted);
    if(!row.sub.empty()){float sy=yy+54*S;int count=0;for(const auto& line:wrapText(ui_,row.sub,w-48*S,18*S,false)){if(count++>=2)break;ui_.text(false,line,x+24*S+slide,sy,18*S,kMuted);sy+=22*S;}}
    if(row.enabled)uiRects_.push_back({Vec4(x+slide,yy,w,height),row.id});
  };
  for(int i=menuPage_*capacity;i<std::min((int)rows.size(),(menuPage_+1)*capacity);++i){renderRow(rows[i],y,rowH);y+=rowH+10*S;}
  if(back)renderRow(footer,screenH_-78*S,58*S);
  if(menuPages_>1){float py=screenH_-122*S;float bw=120*S;
    for(int dir=0;dir<2;++dir){float bx=x+(dir? w-bw:0);ui_.rect(bx,py,bw,35*S,C(1,1,1,0.11f),4*S);ui_.text(false,dir?"Próxima":"Anterior",bx+12*S,py+7*S,18*S,kWhite);uiRects_.push_back({Vec4(bx,py,bw,35*S),790+dir});}
    ui_.text(false,std::to_string(menuPage_+1)+" / "+std::to_string(menuPages_),x+w/2,py+7*S,18*S,kMuted,Align::Center);}
  ui_.text(false,"0.4.0  •  "+std::string(sessionActive_?"Partida em andamento":"Pronto para explorar"),left,screenH_-45*S,17*S,kMuted);
}

void Game::menuAction(int id) {
  if(id==304){
    if(menu_==MenuState::Controls){writeSettings();menu_=MenuState::Settings;controlDrag_=-1;}
    else if(menu_==MenuState::Settings){writeSettings();menu_=settingsBack_;}
    else if(menu_==MenuState::Slots)menu_=MenuState::Main;
    else menu_=MenuState::Pause;
  }else if(id==200)menu_=MenuState::None;
  else if(id==201){toast(saveGame()?"Jogo salvo":"Falha ao salvar", "save");}
  else if(id==202)openSettingsMenu();
  else if(id==203){if(!sessionActive_||saveGame())quit_=true;else toast("Falha ao salvar", "save");}
  else if(id==204){menu_=MenuState::Map;mapCenter_={};mapZoom_=1;}
  else if(id==205)menu_=MenuState::Inventory;
  else if(id==206)menu_=MenuState::Codes;
  else if(id==207)returnToMain();
  else if(id==400){int s=recentSlot();if(s>=0)startSlot(s,false);}
  else if(id==401||id==402){menu_=MenuState::Slots;selectingNew_=id==401;selectedSlot_=0;}
  else if(id>=410&&id<414)selectedSlot_=id-410;
  else if(id==420){if(selectingNew_&&inspectSlot(selectedSlot_).exists){confirmDelete_=false;menu_=MenuState::Confirm;}else startSlot(selectedSlot_,selectingNew_);}
  else if(id==421){confirmDelete_=true;menu_=MenuState::Confirm;}
  else if(id==430){if(confirmDelete_){toast(deleteSlot(selectedSlot_)?"Save excluído":"Não foi possível excluir", "save");menu_=MenuState::Slots;}else startSlot(selectedSlot_,true,true);}
  else if(id==431)menu_=MenuState::Slots;
  else if(id>500&&id<500+kItemCount)useItem(id-500);
  else if(id==520)inventoryTab_=1-inventoryTab_;
  else if(id>=530&&id<530+kWeaponCount) {equipWeapon(id-530);toast(std::string("Equipada: ")+weaponDef(id-530).name,"bag");}
  else if(id>=600&&id<608)executeCode(id-600);
  else if(id>=700&&id<705){settingsTab_=id-700;menuPage_=0;}
  else if(id==790)menuPage_=std::max(0,menuPage_-1);
  else if(id==791)menuPage_=std::min(menuPages_-1,menuPage_+1);
  else if(id==728){menu_=MenuState::Controls;controlDrag_=-1;}
  else if(id==810){for(auto& p:settings_.controlPos)p={-1,-1};settings_.controlScale=1;settings_.leftHanded=false;settings_.fixedJoystick=false;writeSettings();}
  else if(id==901)mapZoom_=std::min(8.0f,mapZoom_*1.4f);
  else if(id==902)mapZoom_=std::max(1.0f,mapZoom_/1.4f);
  else if(id==903){
    mapCenter_=player_.pos;
    if(player_.indoors){int index=world_.interiorAt(player_.pos.x,player_.pos.y);if(index>=0){int shop=world_.interiors[index].shop;if(shop>=0&&shop<(int)world_.shops.size())mapCenter_={world_.shops[shop].door.x,world_.shops[shop].door.z};}}
    mapZoom_=4;
  }
  else if(menu_==MenuState::Settings){
    if(id==300)settings_.invertY=!settings_.invertY;
    if(id==301)settings_.shadows=!settings_.shadows;
    if(id==302)settings_.showFps=!settings_.showFps;
    if(id==303){settings_.quality=(settings_.quality+1)%4;settings_.reflections=settings_.quality>=2?2:(settings_.quality==1?1:0);}
    if(id==305)settings_.dynamicRes=!settings_.dynamicRes;
    if(id==710)settings_.resolution=settings_.resolution<0.99f?std::min(1.0f,settings_.resolution+0.1f):0.6f;
    if(id==711)settings_.vegetation=settings_.vegetation<0.99f?std::min(1.0f,settings_.vegetation+0.25f):0.25f;
    if(id==712)settings_.renderDistance=settings_.renderDistance<1.39f?settings_.renderDistance+0.2f:0.6f;
    if(id==713)settings_.effects=!settings_.effects;
    if(id==714)settings_.volume=settings_.volume<0.99f?std::min(1.0f,settings_.volume+0.1f):0;
    if(id==715)settings_.sensitivity=settings_.sensitivity<1.99f?settings_.sensitivity+0.2f:0.4f;
    if(id==716)settings_.dayCycle=!settings_.dayCycle;
    if(id==718)settings_.fpsLimit=settings_.fpsLimit==30?60:30;
    if(id==729)settings_.weatherMode=(settings_.weatherMode+1)%3;
    if(id==730)settings_.ambientOcclusion=!settings_.ambientOcclusion;
    if(id==719)settings_.reflections=(settings_.reflections+1)%3;
    if(id==720)settings_.controlScale=settings_.controlScale<1.39f?settings_.controlScale+0.1f:0.7f;
    if(id==721)settings_.controlOpacity=settings_.controlOpacity<0.99f?settings_.controlOpacity+0.1f:0.3f;
    if(id==722)settings_.highContrast=!settings_.highContrast;
    if(id==723)settings_.subtitles=!settings_.subtitles;
    if(id==724)settings_.soundCaptions=!settings_.soundCaptions;
    if(id==725)settings_.reducedMotion=!settings_.reducedMotion;
    if(id==726)settings_.fixedJoystick=!settings_.fixedJoystick;
    if(id==727){settings_.leftHanded=!settings_.leftHanded;for(auto& p:settings_.controlPos)p={-1,-1};}
    if(id==717)settings_.hudScale=settings_.hudScale<1.34f?settings_.hudScale+0.1f:0.75f;
    applySettings();writeSettings();
  }
}

void Game::handleUiPointers(const InputFrame& in) {
  auto hit=[&](Vec2 p){for(auto it=uiRects_.rbegin();it!=uiRects_.rend();++it){const auto&r=it->first;if(p.x>=r.x&&p.x<=r.x+r.z&&p.y>=r.y&&p.y<=r.y+r.w)return it->second;}return -1;};
  for(const auto&p:in.ui){
    if(p.pressed){pressedUi_=hit(p.pos);if(menu_==MenuState::Controls&&pressedUi_>=800&&pressedUi_<809)controlDrag_=pressedUi_-800;if(menu_==MenuState::Map&&pressedUi_==900){mapDragging_=true;mapDrag_=p.pos;}}
    if(menu_==MenuState::Controls&&controlDrag_>=0&&p.down){settings_.controlPos[controlDrag_]={clamp(p.pos.x/screenW_,0.05f,0.95f),clamp(p.pos.y/screenH_,0.12f,0.9f)};if(controlDrag_==8)settings_.fixedJoystick=true;}
    if(menu_==MenuState::Map&&mapDragging_&&p.down){
      float size=std::min(screenH_-180*uiScale(),screenW_-460*uiScale());
      mapCenter_=mapCenter_-(p.pos-mapDrag_)*(2*mapExtent_/mapZoom_/size);mapDrag_=p.pos;
      mapCenter_.x=clamp(mapCenter_.x,-mapExtent_,mapExtent_);mapCenter_.y=clamp(mapCenter_.y,-mapExtent_,mapExtent_);
    }
    if(p.released){int id=hit(p.pos);mapDragging_=false;if(controlDrag_>=0){controlDrag_=-1;writeSettings();pressedUi_=-1;continue;}
      if(id>=0&&id==pressedUi_){if(menu_!=MenuState::None)menuAction(id);else if(panel_.open&&id<100)selectPanelOption(id);}
      pressedUi_=-1;
    }
  }
}

void Game::drawDebug() {
  if (!settings_.showFps) return;
  float S = uiScale();
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%.0f FPS  %.1f ms  scale %.2f  chunks %d  sprites %d  npc %d/%d/%d", fpsShown_, frameMsAvg_, r_->renderScale(), stats_.drawnChunks,
                stats_.drawnSprites, stats_.npcNear, stats_.npcMid, stats_.npcFar);
  ui_.rect(30 * S, screenH_ - 54 * S, ui_.textWidth(false, buf, 22 * S) + 24 * S, 38 * S, C(0, 0, 0, 0.55f), 10 * S);
  ui_.text(false, buf, 42 * S, screenH_ - 48 * S, 22 * S, kMint, Align::Left);
}

void Game::buildUi(gfx::FrameData& fd, float dt) {
  ui_.begin(&fd, &assets_, screenW_, screenH_, uiScale(),settings_.highContrast);
  InputFrame in;   // draw uses the polled state stored by frame(); rebuild a lightweight view for visuals
  in = useScripted_ ? scripted_ : InputFrame();
  if (!useScripted_) {
    // The joystick / button visuals need the last polled values: poll() already consumed edges, so recompute from a
    // non-destructive snapshot kept in lastVisual_.
  }
  in = visualInput_;
  if (sessionActive_ && menu_ == MenuState::None && !panel_.open) drawHud(dt, in);

  if (wheel_.anim > 0.01f) drawWheel(dt);
  if (panel_.open) drawPanel(dt);
  if (menu_ != MenuState::None){drawMenus(dt);drawToasts(dt);}
  drawSubtitles();
  drawDebug();
  ui_.end();
}

}  // namespace gtabr
