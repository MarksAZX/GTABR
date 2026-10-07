// HUD, touch controls, minimap, panels, radial wheel, pause/settings menus.
#include <algorithm>
#include <cmath>

#include "game.h"

namespace gtabr {

namespace {
const Color kWhite = 0xFFFFFFFFu;
Color C(float r, float g, float b, float a = 1.0f) { return rgba(r, g, b, a); }
const Color kAccent = rgba(1.0f, 0.80f, 0.26f);
const Color kGlass = rgba(0.045f, 0.055f, 0.08f, 0.60f);
const Color kGlassHi = rgba(0.10f, 0.12f, 0.17f, 0.72f);
const Color kMint = rgba(0.36f, 0.89f, 0.66f);
const Color kRed = rgba(1.0f, 0.36f, 0.40f);
const Color kSky = rgba(0.42f, 0.72f, 1.0f);
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
    if (c == ' ') flushWord();
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
  float S = uiScale();
  L.width = screenW_; L.height = screenH_;
  L.joyZoneRight = screenW_ * 0.44f;
  L.joyRadius = 105.0f * S;
  Vec2 A{screenW_ - 230.0f * S, screenH_ - 200.0f * S};
  L.run = {A + Vec2{30, 40} * S, 88.0f * S, true};
  L.interact = {A + Vec2{-130, -20} * S, 70.0f * S, true};
  L.enterExit = {A + Vec2{-10, -150} * S, 62.0f * S, true};
  L.camera = {A + Vec2{-250, 70} * S, 46.0f * S, true};
  L.wheel = {A + Vec2{-250, -105} * S, 48.0f * S, true};
  L.pause = {Vec2{screenW_ - 64.0f * S, 64.0f * S}, 34.0f * S, true};
  L.modal = panel_.open || menu_ != MenuState::None;
  if (L.modal) { L.run.visible = L.interact.visible = L.enterExit.visible = L.camera.visible = L.wheel.visible = L.pause.visible = false; }
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
    ui_.text(false, "Um MVP urbano brasileiro", cx, cy + 20 * S, 32 * S, kMuted, Align::Center);
  }
  float p = assets_.progress() * 0.85f + (worldReady_ ? 0.15f : 0.0f);
  float bw = 520 * S, bh = 8 * S;
  ui_.rect(cx - bw / 2, cy + 100 * S, bw, bh, C(1, 1, 1, 0.14f), bh / 2);
  ui_.rect(cx - bw / 2, cy + 100 * S, bw * clamp(p, 0.02f, 1.0f), bh, kAccent, bh / 2);
  if (hasFont) {
    static const char* tips[] = {"Dica: o botão de câmera alterna entre Top Down e Terceira Pessoa.", "Dica: o posto fica no quarteirão a nordeste.",
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
  float a = 1.0f - 0.85f * wheel_.anim;
  bool driving = player_.vehicle >= 0;
  // joystick
  if (in.joyActive) {
    ui_.circle(in.joyBase.x, in.joyBase.y, L.joyRadius, C(0.05f, 0.06f, 0.09f, 0.28f * a), 2.0f * S, C(1, 1, 1, 0.38f * a));
    ui_.circle(in.joyKnob.x, in.joyKnob.y, L.joyRadius * 0.42f, C(1, 1, 1, 0.38f * a), 2.0f * S, C(1, 1, 1, 0.8f * a));
  } else {
    Vec2 h{L.joyRadius * 1.6f, screenH_ - L.joyRadius * 1.9f};
    ui_.circle(h.x, h.y, L.joyRadius, C(0.05f, 0.06f, 0.09f, 0.16f * a), 2.0f * S, C(1, 1, 1, 0.20f * a));
    ui_.circle(h.x, h.y, L.joyRadius * 0.42f, C(1, 1, 1, 0.14f * a), 0, 0);
  }
  glassButton(ui_, L.run.c, L.run.r, in.runHeld, false, driving ? "target" : "run", kAccent, a, 0.78f);
  ui_.text(false, driving ? "FREIO" : "CORRER", L.run.c.x, L.run.c.y + L.run.r * 0.52f, 17 * S, C(1, 1, 1, 0.75f * a), Align::Center);
  bool can = focusValid_;
  float pulse = can ? 0.5f + 0.5f * std::sin(realTime_ * 5.0f) : 0.0f;
  if (can) ui_.circle(L.interact.c.x, L.interact.c.y, L.interact.r + (5 + 5 * pulse) * S, C(1, 1, 1, 0), 2.0f * S, withAlpha(kAccent, (0.5f - 0.3f * pulse) * a));
  glassButton(ui_, L.interact.c, L.interact.r, in.interactHeld, can, can ? focus_.icon : "hand", kAccent, can ? a : a * 0.55f);
  bool canEnter = driving || nearestVehicleTo(player_.pos, 2.4f) >= 0 || (player_.indoors && false);
  glassButton(ui_, L.enterExit.c, L.enterExit.r, in.enterExitHeld, canEnter, driving ? "door" : "car", kMint, canEnter ? a : a * 0.55f);
  glassButton(ui_, L.camera.c, L.camera.r, in.cameraHeld, false, "camera", kAccent, a);
  glassButton(ui_, L.wheel.c, L.wheel.r, in.wheelBtnHeld, wheel_.open, "wheel", kAccent, 1.0f - 0.0f * wheel_.anim);
  ui_.text(false, "ITENS", L.wheel.c.x, L.wheel.c.y + L.wheel.r + 6 * S, 16 * S, C(1, 1, 1, 0.7f * a), Align::Center);
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
    marker({world_.poiMarketDoor.x, world_.poiMarketDoor.z}, "cart", kAccent, 30 * S, true);
    marker({world_.poiWorkshop.x, world_.poiWorkshop.z}, "wrench", kSky, 30 * S, true);
    for (const Vehicle& v : vehicles_)
      if (player_.vehicle != v.id) marker(v.pos, "car", C(1, 1, 1, 0.95f), 22 * S, false);
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
  float hAlpha = healthShow_ > 0.0f ? 1.0f : 0.40f;
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
  for (const Toast& t : toasts_) {
    float in = clamp(t.t / 0.25f, 0.0f, 1.0f), out = clamp((t.dur + 0.4f - t.t) / 0.4f, 0.0f, 1.0f);
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
  panel_.anim += (1.0f - panel_.anim) * expDecay(14.0f, dt);
  float S = uiScale();
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
  const float gap = 0.045f;
  // outer sectors
  for (int i = 0; i < 8; ++i) {
    float a0 = i * kTau / 8 - kTau / 16 + gap, a1 = (i + 1) * kTau / 8 - kTau / 16 - gap;
    bool has = i < (int)slots.size();
    bool hov = wheel_.hovered == i;
    Color col = hov ? withAlpha(kAccent, 0.88f * a) : C(0.05f, 0.06f, 0.09f, (has ? 0.62f : 0.30f) * a);
    ui_.arc(c.x, c.y, R * 0.52f, R, a0, a1, col);
    float am = (a0 + a1) * 0.5f;
    Vec2 p{c.x + std::sin(am) * R * 0.76f, c.y - std::cos(am) * R * 0.76f};
    if (has) {
      const ItemDef& d = itemDef(slots[i]);
      if (d.art) ui_.art(d.art, p.x - 40 * S, p.y - 40 * S, 80 * S, 80 * S, C(1, 1, 1, a * (hov ? 1.0f : 0.92f)), 16 * S);
      else ui_.icon(d.icon, p.x, p.y, 62 * S, hov ? C(0.1f, 0.1f, 0.12f, a) : C(1, 1, 1, a));
      if (d.category == ItemCategory::Item) {
        std::string cnt = "x" + std::to_string(inventory_[slots[i]]);
        ui_.text(true, cnt, p.x + 30 * S, p.y + 18 * S, 24 * S, hov ? C(0.1f, 0.1f, 0.12f, a) : C(1, 1, 1, a), Align::Center, C(0, 0, 0, 0.7f), 0.1f);
      }
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
  // centre info
  if (wheel_.hovered >= 0 && wheel_.hovered < (int)slots.size()) {
    const ItemDef& d = itemDef(slots[wheel_.hovered]);
    ui_.text(true, d.name, c.x, c.y - 24 * S, 28 * S, C(1, 1, 1, a), Align::Center, C(0, 0, 0, 0.6f), 0.1f);
  } else {
    ui_.text(false, wheel_.category == 0 ? "Armas" : "Itens", c.x, c.y - 14 * S, 22 * S, C(1, 1, 1, 0.55f * a), Align::Center);
  }
  if (slots.empty()) ui_.text(false, wheel_.category == 0 ? "Sem armas" : "Nenhum item — compre no mercado", c.x, c.y + R * 1.06f, 24 * S, C(1, 1, 1, 0.65f * a), Align::Center);
  else if (wheel_.hovered >= 0) ui_.text(false, itemDef(slots[wheel_.hovered]).desc, c.x, c.y + R * 1.06f, 24 * S, C(1, 1, 1, 0.8f * a), Align::Center);
  // finger marker
  Vec2 f = wheel_.finger - c;
  float fl = f.length();
  if (fl > R * 1.1f) f = f * (R * 1.1f / fl);
  ui_.circle(c.x + f.x, c.y + f.y, 14 * S, C(1, 1, 1, 0.85f * a), 2 * S, withAlpha(kAccent, a));
  ui_.rect(c.x - 1.5f * S, c.y - 1.5f * S, 3 * S, 3 * S, C(1, 1, 1, 0.5f * a), 1.5f * S);
}

// ------------------------------------------------------------------------------------------------ menus
void Game::openSettingsMenu() { menu_ = MenuState::Settings; }

void Game::drawMenus(float dt) {
  (void)dt;
  if (menu_ == MenuState::None) return;
  float S = uiScale();
  uiRects_.clear();
  ui_.rect(0, 0, screenW_, screenH_, C(0.01f, 0.015f, 0.03f, 0.62f));
  float cardW = std::min(760.0f * S, screenW_ - 80 * S);
  float x = (screenW_ - cardW) / 2;
  auto button = [&](float y, float h, const std::string& label, const char* icon, int id, bool accent = false) {
    float rx = x + 34 * S, rw = cardW - 68 * S;
    bool pressed = pressedUi_ == id;
    ui_.rect(rx, y, rw, h, pressed ? C(1, 1, 1, 0.22f) : (accent ? withAlpha(kAccent, 0.22f) : C(1, 1, 1, 0.10f)), 22 * S, 1.2f * S, accent ? withAlpha(kAccent, 0.7f) : C(1, 1, 1, 0.14f));
    if (icon) ui_.icon(icon, rx + 46 * S, y + h / 2, 40 * S, accent ? kAccent : C(1, 1, 1, 0.9f));
    ui_.text(true, label, rx + (icon ? 90 : 34) * S, y + h / 2 - 20 * S, 34 * S, kWhite, Align::Left);
    uiRects_.push_back({Vec4(rx, y, rw, h), id});
  };
  if (menu_ == MenuState::Pause) {
    float h = 520 * S, y = (screenH_ - h) / 2;
    ui_.glow(x, y + 8, cardW, h, 30 * S, 28 * S, C(0, 0, 0, 0.5f));
    ui_.rect(x, y, cardW, h, C(0.05f, 0.06f, 0.09f, 0.9f), 30 * S, 1.6f * S, C(1, 1, 1, 0.16f));
    ui_.text(true, "PAUSADO", x + cardW / 2, y + 28 * S, 54 * S, kWhite, Align::Center);
    float by = y + 120 * S, bh = 78 * S, bg = 14 * S;
    button(by, bh, "Continuar", "play", 200, true); by += bh + bg;
    button(by, bh, "Salvar jogo", "save", 201); by += bh + bg;
    button(by, bh, "Configurações", "gear", 202); by += bh + bg;
    button(by, bh, "Sair do jogo", "close", 203);
    ui_.text(false, "Dinheiro " + fmtMoney(moneyCents_) + "   •   Progresso salvo automaticamente", x + cardW / 2, y + h - 40 * S, 22 * S, kMuted, Align::Center);
  } else {
    float h = 790 * S, y = (screenH_ - h) / 2;
    ui_.glow(x, y + 8, cardW, h, 30 * S, 28 * S, C(0, 0, 0, 0.5f));
    ui_.rect(x, y, cardW, h, C(0.05f, 0.06f, 0.09f, 0.92f), 30 * S, 1.6f * S, C(1, 1, 1, 0.16f));
    ui_.text(true, "CONFIGURAÇÕES", x + cardW / 2, y + 26 * S, 46 * S, kWhite, Align::Center);
    float ry = y + 110 * S, rh = 84 * S;
    auto slider = [&](const char* label, float v, float lo, float hi, int id, const std::string& valueText) {
      float rx = x + 34 * S, rw = cardW - 68 * S;
      ui_.rect(rx, ry, rw, rh - 10 * S, C(1, 1, 1, 0.07f), 20 * S);
      ui_.text(true, label, rx + 24 * S, ry + 8 * S, 28 * S, kWhite, Align::Left);
      ui_.text(false, valueText, rx + rw - 24 * S, ry + 8 * S, 24 * S, kMuted, Align::Right);
      float tx = rx + 24 * S, tw = rw - 48 * S, ty = ry + rh - 30 * S;
      float t = clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
      ui_.rect(tx, ty - 4 * S, tw, 8 * S, C(1, 1, 1, 0.16f), 4 * S);
      ui_.rect(tx, ty - 4 * S, tw * t, 8 * S, kAccent, 4 * S);
      ui_.circle(tx + tw * t, ty, 15 * S, kWhite, 3 * S, kAccent);
      uiRects_.push_back({Vec4(tx - 20 * S, ty - 34 * S, tw + 40 * S, 68 * S), id});
      ry += rh;
    };
    auto toggle = [&](const char* label, bool on, int id, const std::string& txt = "") {
      float rx = x + 34 * S, rw = cardW - 68 * S;
      bool pressed = pressedUi_ == id;
      ui_.rect(rx, ry, rw, rh - 10 * S, pressed ? C(1, 1, 1, 0.2f) : C(1, 1, 1, 0.07f), 20 * S);
      ui_.text(true, label, rx + 24 * S, ry + (rh - 10 * S) / 2 - 18 * S, 28 * S, kWhite, Align::Left);
      if (!txt.empty()) ui_.text(true, txt, rx + rw - 24 * S, ry + (rh - 10 * S) / 2 - 16 * S, 26 * S, kAccent, Align::Right);
      else {
        float sw = 86 * S, sh = 44 * S, sx = rx + rw - sw - 24 * S, sy = ry + (rh - 10 * S) / 2 - sh / 2;
        ui_.rect(sx, sy, sw, sh, on ? withAlpha(kMint, 0.85f) : C(1, 1, 1, 0.2f), sh / 2);
        ui_.circle(sx + (on ? sw - sh / 2 : sh / 2), sy + sh / 2, sh / 2 - 4 * S, kWhite);
      }
      uiRects_.push_back({Vec4(rx, ry, rw, rh - 10 * S), id});
      ry += rh;
    };
    slider("Sensibilidade da câmera", settings_.sensitivity, 0.4f, 2.0f, 100, fmtFloat(settings_.sensitivity, 2) + "x");
    slider("Tamanho dos controles", settings_.hudScale, 0.75f, 1.35f, 101, std::to_string((int)(settings_.hudScale * 100)) + "%");
    static const char* q[4] = {"Automática", "Baixa", "Média", "Alta"};
    toggle("Qualidade gráfica", false, 303, q[settings_.quality]);
    toggle("Sombras dinâmicas", settings_.shadows, 301);
    toggle("Inverter eixo Y da câmera", settings_.invertY, 300);
    toggle("Mostrar FPS", settings_.showFps, 302);
    button(ry + 10 * S, 74 * S, "Voltar", "arrow", 304, true);
  }
}

void Game::handleUiPointers(const InputFrame& in) {
  auto hit = [&](Vec2 p) {
    for (auto it = uiRects_.rbegin(); it != uiRects_.rend(); ++it) {
      const Vec4& r = it->first;
      if (p.x >= r.x && p.x <= r.x + r.z && p.y >= r.y && p.y <= r.y + r.w) return it->second;
    }
    return -1;
  };
  auto applySlider = [&](int slider, float px) {
    for (const auto& pr : uiRects_)
      if (pr.second == slider) {
        float tx = pr.first.x + 20 * uiScale(), tw = pr.first.z - 40 * uiScale();
        float t = clamp((px - tx) / tw, 0.0f, 1.0f);
        if (slider == 100) settings_.sensitivity = 0.4f + t * 1.6f;
        if (slider == 101) settings_.hudScale = 0.75f + t * 0.6f;
      }
  };
  for (const UiPointer& p : in.ui) {
    if (p.pressed) {
      pressedUi_ = hit(p.pos);
      if (pressedUi_ == 100 || pressedUi_ == 101) { activeSlider_ = pressedUi_; applySlider(activeSlider_, p.pos.x); }
    } else if (p.down && activeSlider_ >= 0) {
      applySlider(activeSlider_, p.pos.x);
    }
    if (p.released) {
      if (activeSlider_ >= 0) { applySlider(activeSlider_, p.pos.x); activeSlider_ = -1; pressedUi_ = -1; applySettings(); continue; }
      int id = hit(p.pos);
      if (id >= 0 && id == pressedUi_) {
        if (menu_ == MenuState::Pause) {
          if (id == 200) menu_ = MenuState::None;
          else if (id == 201) { saveGame(); toast("Jogo salvo", "save"); menu_ = MenuState::None; }
          else if (id == 202) menu_ = MenuState::Settings;
          else if (id == 203) { saveGame(); quit_ = true; }
        } else if (menu_ == MenuState::Settings) {
          if (id == 300) settings_.invertY = !settings_.invertY;
          else if (id == 301) settings_.shadows = !settings_.shadows;
          else if (id == 302) settings_.showFps = !settings_.showFps;
          else if (id == 303) settings_.quality = (settings_.quality + 1) % 4;
          else if (id == 304) { menu_ = MenuState::Pause; saveGame(); }
          applySettings();
        } else if (panel_.open && id >= 0 && id < 100) {
          selectPanelOption(id);
        }
      }
      pressedUi_ = -1;
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
  ui_.begin(&fd, &assets_, screenW_, screenH_, uiScale());
  InputFrame in;   // draw uses the polled state stored by frame(); rebuild a lightweight view for visuals
  in = useScripted_ ? scripted_ : InputFrame();
  if (!useScripted_) {
    // The joystick / button visuals need the last polled values: poll() already consumed edges, so recompute from a
    // non-destructive snapshot kept in lastVisual_.
  }
  in = visualInput_;
  drawHud(dt, in);
  if (wheel_.anim > 0.01f) drawWheel(dt);
  if (panel_.open) drawPanel(dt);
  if (menu_ != MenuState::None) drawMenus(dt);
  drawDebug();
  ui_.end();
}

}  // namespace gtabr
