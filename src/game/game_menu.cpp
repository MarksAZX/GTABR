// Main menu, save-slot screens, pause menu (map / game / inventory / codes / settings / save / main menu), confirmations and the
// full-screen map. Everything is drawn with the immediate-mode painter; touch handling mirrors the HUD (tap = press + release on
// the same rect without travelling, drag = map pan / slider).
#include <algorithm>
#include <cmath>
#include <ctime>

#include "../core/log.h"
#include "game.h"
#include "ui_theme.h"

namespace gtabr {

using namespace theme;

namespace {
const Color kWhite = 0xFFFFFFFFu;
const char* kDistrictNames[] = {"Centro", "Residencial", "Comercial", "Orla", "Parque", "Distrito Industrial"};
const char* kAvenueNames[] = {"Av. Beira-Mar", "Av. Central", "Av. das Palmeiras", "Av. Brasil", "Av. Atlântica", "Av. Getúlio Vargas"};
const char* kStreetNames[] = {"Rua das Flores", "Rua do Comércio", "Rua São Jorge", "Rua da Paz", "Rua Sete de Setembro", "Rua das Acácias",
                              "Rua do Sol", "Rua Marechal Deodoro", "Rua dos Pescadores", "Rua Bahia", "Rua Pará", "Rua Rio Branco"};

std::string fmtClock(int secs) {
  int h = secs / 3600, m = (secs / 60) % 60;
  char b[32];
  std::snprintf(b, sizeof(b), "%02d:%02d", h, m);
  return b;
}

std::string fmtDate(long long t) {
  if (t <= 0) return "-";
  std::time_t tt = (std::time_t)t;
  std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &tt);
#else
  localtime_r(&tt, &tmv);
#endif
  char b[48];
  std::snprintf(b, sizeof(b), "%02d/%02d/%04d  %02d:%02d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900, tmv.tm_hour, tmv.tm_min);
  return b;
}

std::string upper(std::string s) {
  // ASCII + the accented capitals the UI font carries
  static const std::pair<const char*, const char*> acc[] = {{"á", "Á"}, {"à", "À"}, {"â", "Â"}, {"ã", "Ã"}, {"é", "É"}, {"ê", "Ê"}, {"í", "Í"},
                                                            {"ó", "Ó"}, {"ô", "Ô"}, {"õ", "Õ"}, {"ú", "Ú"}, {"ç", "Ç"}};
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { out.push_back((char)std::toupper(c)); continue; }
    bool done = false;
    for (auto& a : acc) {
      size_t n = std::strlen(a.first);
      if (s.compare(i, n, a.first) == 0) { out += a.second; i += n - 1; done = true; break; }
    }
    if (!done) out.push_back((char)c);
  }
  return out;
}
}  // namespace

// ------------------------------------------------------------------------------------------------ place names
std::string Game::locationName(Vec2 p, bool indoors) const {
  if (indoors) {
    int r = world_.interiorAt(p.x, p.y);
    if (r >= 0 && world_.interiors[r].shop >= 0 && world_.interiors[r].shop < (int)world_.shops.size()) return world_.shops[world_.interiors[r].shop].name;
    return "Interior";
  }
  if (world_.coastSide >= 0) {
    if (world_.waterDepth(p.x, p.y) > 0.3f) return "Mar de " + world_.cityName;
    if (world_.beach.contains(p.x, p.y)) return "Praia de " + world_.cityName;
  }
  for (const auto& b : world_.blocks)
    if (b.first.contains(p.x, p.y)) return kDistrictNames[(int)b.second];
  for (size_t i = 0; i < world_.roads.size(); ++i) {
    const RoadLine& r = world_.roads[i];
    float along = r.horizontal ? p.x : p.y, across = r.horizontal ? p.y : p.x;
    if (std::fabs(across - r.c) <= r.hw && along >= r.a && along <= r.b)
      return r.avenue ? kAvenueNames[(world_.seed + i) % 6] : kStreetNames[(world_.seed * 7 + i) % 12];
  }
  return world_.cityName;
}

// ------------------------------------------------------------------------------------------------ menu scene
void Game::enterMainMenu() {
  if (phase_ == Phase::Playing) saveGame();
  for (int* h : {&surfHandle_, &rainHandle_, &sirenHandle_})
    if (*h) { audio_.loopStop(*h); *h = 0; }
  resetEntities(true);
  phase_ = Phase::Menu;
  menu_ = MenuState::Main;
  settingsOnly_ = false;
  confirm_.open = false;
  panel_ = Panel();
  wheel_.open = false;
  refreshSlots();
  menuT_ = 0;
  menuCamT_ = 0;
  menuStreamFirst_ = true;
  timeOfDay_ = 17.35f;               // golden hour behind the menu
  rain_ = weatherTarget_ = wetness_ = 0.0f;
  weatherMode_ = 1;                  // calm sky while on the menu
  cam_.init(CamMode::ThirdPerson);
  cam_.setMode(CamMode::ThirdPerson);
  applyAudioSettings();
}

void Game::updateMenuScene(float dt) {
  menuT_ += dt;
  menuCamT_ += dt;
  updateNpcs(dt);
  // slow cinematic orbit around the beach (or the park when the city has no coast)
  Vec3 base = world_.coastSide >= 0 ? world_.poiBeach : world_.poiPlaza;
  if (world_.coastSide >= 0) {
    // stand on the sand a little inland of the water line so the camera looks along the beach, over the sea and the city
    Vec2 sea = world_.coastSide == 0 ? Vec2{0, -1} : (world_.coastSide == 1 ? Vec2{1, 0} : (world_.coastSide == 2 ? Vec2{0, 1} : Vec2{-1, 0}));
    base.x -= sea.x * 16.0f; base.z -= sea.y * 16.0f;
  }
  float t = menuCamT_;
  CameraInput ci;
  ci.focus = {base.x + std::cos(t * 0.05f) * 14.0f, base.y + 3.4f, base.z + std::sin(t * 0.05f) * 14.0f};
  ci.headingYaw = t * 0.06f;
  ci.look = {0.55f, 0.0f};
  cam_.update(dt, ci, world_, screenW_ / std::max(1.0f, screenH_));
  // keep the player (and its prompts / shadows) away from the menu view
  player_.pos = {world_.spawnPlayer.x, world_.spawnPlayer.z};
}

// ------------------------------------------------------------------------------------------------ small widgets
namespace {
struct Ctx {
  UiPainter& ui;
  std::vector<std::pair<Vec4, int>>& rects;
  float S;
  int pressed;
  bool hc;
};
Color dimCol(const Ctx& c) { return c.hc ? kDimHi : kDim; }

// flat button with a hairline border: primary = filled light, otherwise glass
void pill(Ctx& c, float x, float y, float w, float h, const std::string& label, int id, bool primary = false, bool enabled = true, Color tint = 0) {
  bool pr = c.pressed == id;
  Color fill = primary ? (pr ? rgba(1, 1, 1, 0.92f) : rgba(0.93f, 0.95f, 0.98f, 0.88f)) : (pr ? kRowHot : rgba(1, 1, 1, 0.06f));
  Color border = primary ? rgba(1, 1, 1, 0.0f) : (tint ? withAlpha(tint, 0.7f) : kLineHi);
  if (!enabled) { fill = rgba(1, 1, 1, 0.03f); border = kLine; }
  c.ui.rect(x, y, w, h, fill, h * 0.5f, primary ? 0 : 1.2f * c.S, border);
  Color tc = primary ? rgba(0.04f, 0.05f, 0.07f) : (enabled ? (tint ? tint : kInk) : kFaint);
  c.ui.text(true, label, x + w * 0.5f, y + h * 0.5f - 11.5f * c.S, 21 * c.S, tc, Align::Center);
  if (enabled) c.rects.push_back({Vec4(x, y, w, h), id});
}
}  // namespace

// ------------------------------------------------------------------------------------------------ main menu
void Game::drawMainMenu(float dt) {
  (void)dt;
  const float S = uiScale();
  uiRects_.clear();
  Ctx c{ui_, uiRects_, S, pressedUi_, settings_.highContrast};
  const float W = screenW_, H = screenH_;
  float fade = smoothstep(clamp(menuT_ / 0.8f, 0.0f, 1.0f));
  // cinematic grading: dark left side for the menu, soft top / bottom bars
  ui_.hgradient(0, 0, W * 0.34f, H, rgba(0, 0, 0, 0.84f * fade), rgba(0, 0, 0, 0.62f * fade));
  ui_.hgradient(W * 0.34f, 0, W * 0.30f, H, rgba(0, 0, 0, 0.62f * fade), rgba(0, 0, 0, 0));
  ui_.gradient(0, 0, W, H * 0.16f, rgba(0, 0, 0, 0.55f * fade), rgba(0, 0, 0, 0));
  ui_.gradient(0, H * 0.82f, W, H * 0.18f, rgba(0, 0, 0, 0), rgba(0, 0, 0, 0.6f * fade));

  float lx = W * 0.075f;
  ui_.text(true, "BAIRRO", lx, H * 0.105f, 118 * S, withAlpha(kInk, fade), Align::Left);
  ui_.rect(lx, H * 0.105f + 132 * S, 84 * S, 3 * S, withAlpha(kAcc, fade), 1.5f * S);
  ui_.text(false, upper(world_.cityName) + "   ·   SEED " + std::to_string(worldSeed_), lx, H * 0.105f + 150 * S, 22 * S, withAlpha(dimCol(c), fade), Align::Left);

  if (menu_ == MenuState::Main) {
    int last = latestSlot();
    SlotInfo li = last ? slots_[last - 1] : SlotInfo{};
    struct Item { const char* label; int id; bool enabled; std::string sub; };
    std::vector<Item> items = {
        {"CONTINUAR", 2000, last > 0, last ? "Espaço " + std::to_string(last) + "  ·  " + li.city + "  ·  " + li.location + "  ·  " + fmtClock(li.playSecs) : "Nenhum jogo salvo"},
        {"NOVO JOGO", 2001, true, "Gera uma cidade nova a partir de uma seed"},
        {"CARREGAR JOGO", 2002, false, ""},
        {"CONFIGURAÇÕES", 2003, true, "Gráficos, áudio, controles, jogabilidade, acessibilidade"},
        {"SAIR", 2004, true, ""},
    };
    bool anySave = false;
    for (int i = 0; i < kSlots; ++i) anySave |= slots_[i].used;
    items[2].enabled = anySave;
    items[2].sub = anySave ? "Escolha um dos espaços de save" : "Nenhum jogo salvo";
    float y = H * 0.40f, pitch = 86 * S;
    for (size_t i = 0; i < items.size(); ++i) {
      const Item& it = items[i];
      float a = fade * smoothstep(clamp((menuT_ - 0.15f - 0.07f * i) / 0.5f, 0.0f, 1.0f));
      bool pr = pressedUi_ == it.id;
      float rowW = W * 0.40f;
      if (pr) ui_.rect(lx - 24 * S, y - 8 * S, rowW, pitch - 12 * S, rgba(1, 1, 1, 0.07f * a), 10 * S);
      ui_.rect(lx - 24 * S, y + 6 * S, 3 * S, 40 * S, withAlpha(it.enabled ? kAcc : kFaint, (pr ? 1.0f : 0.0f) * a), 1.5f * S);
      Color col = it.enabled ? withAlpha(kInk, a) : withAlpha(kFaint, a);
      ui_.text(true, it.label, lx, y, 42 * S, col, Align::Left);
      if (!it.sub.empty()) ui_.text(false, it.sub, lx + 2 * S, y + 46 * S, 20 * S, withAlpha(dimCol(c), a * (it.enabled ? 1.0f : 0.7f)), Align::Left);
      if (it.enabled) uiRects_.push_back({Vec4(lx - 24 * S, y - 8 * S, rowW, pitch - 12 * S), it.id});
      y += pitch;
    }
    ui_.text(false, "Toque numa opção para continuar", lx, H - 64 * S, 18 * S, withAlpha(kFaint, fade), Align::Left);
  } else {
    // slot screen
    float pw = std::min(W * 0.62f, 1180.0f * S), px = W - pw - W * 0.045f, py = H * 0.075f, ph = H * 0.85f;
    float pf = smoothstep(clamp(menuT_ / 0.3f, 0.0f, 1.0f));
    ui_.rect(0, 0, W, H, rgba(0, 0, 0, 0.35f * pf));
    ui_.glow(px, py + 10, pw, ph, 22 * S, 34 * S, rgba(0, 0, 0, 0.5f * pf));
    ui_.rect(px, py, pw, ph, withAlpha(c.hc ? kPanelHc : kGlassHi, pf), 22 * S, 1.2f * S, kLine);
    ui_.text(true, slotMode_ == SlotMode::New ? "NOVO JOGO" : "CARREGAR JOGO", px + 40 * S, py + 30 * S, 38 * S, kInk, Align::Left);
    ui_.text(false, slotMode_ == SlotMode::New ? "Escolha o espaço onde a nova cidade será salva" : "Escolha o jogo que deseja continuar",
             px + 40 * S, py + 78 * S, 20 * S, dimCol(c), Align::Left);
    pill(c, px + pw - 190 * S, py + 34 * S, 150 * S, 46 * S, "VOLTAR", 2200);
    drawSlotList(px + 30 * S, py + 118 * S, pw - 60 * S, ph - 140 * S, slotMode_);
  }
}

// ------------------------------------------------------------------------------------------------ slot list
void Game::drawSlotList(float x, float y, float w, float h, SlotMode mode) {
  const float S = uiScale();
  Ctx c{ui_, uiRects_, S, pressedUi_, settings_.highContrast};
  float gap = 12 * S, rh = (h - gap * (kSlots - 1)) / kSlots;
  rh = std::min(rh, 124 * S);
  for (int i = 0; i < kSlots; ++i) {
    const SlotInfo& si = slots_[i];
    float ry = y + i * (rh + gap);
    bool active = (i + 1) == activeSlot_ && phase_ == Phase::Playing;
    int base = 2100 + i * 10;
    bool pr = pressedUi_ == base;
    ui_.rect(x, ry, w, rh, pr ? kRowHot : kRow, 14 * S, 1.2f * S, active ? withAlpha(kAcc, 0.55f) : kLine);
    ui_.text(true, (i + 1 < 10 ? "0" : "") + std::to_string(i + 1), x + 26 * S, ry + rh * 0.5f - 34 * S, 62 * S, si.used ? kInk : kFaint, Align::Left);
    float tx = x + 118 * S;
    if (si.used) {
      ui_.text(true, upper(si.city), tx, ry + 12 * S, 30 * S, kInk, Align::Left);
      ui_.text(false, si.location + "   ·   " + fmtDate(si.saved), tx, ry + 12 * S + 38 * S, 20 * S, dimCol(c), Align::Left);
      ui_.text(false, "Tempo " + fmtClock(si.playSecs) + "   ·   " + fmtMoney(si.money) + "   ·   Seed " + std::to_string(si.seed), tx, ry + 12 * S + 64 * S, 20 * S,
               dimCol(c), Align::Left);
      float bw = std::min(w * 0.38f, 380.0f * S);
      ui_.rect(tx, ry + rh - 14 * S, bw, 4 * S, rgba(1, 1, 1, 0.12f), 2 * S);
      ui_.rect(tx, ry + rh - 14 * S, bw * si.progress, 4 * S, kAcc, 2 * S);
      ui_.text(false, "Progresso " + std::to_string((int)std::lround(si.progress * 100)) + "%", tx + bw + 12 * S, ry + rh - 24 * S, 17 * S, dimCol(c), Align::Left);
    } else {
      ui_.text(false, "Espaço vazio", tx, ry + rh * 0.5f - 14 * S, 26 * S, kFaint, Align::Left);
    }
    if (active) ui_.text(true, "EM JOGO", x + w - 24 * S, ry + 10 * S, 16 * S, kAcc, Align::Right);
    // actions on the right
    float bh = 42 * S, by = ry + rh * 0.5f - bh * 0.5f + (active ? 8 * S : 0);
    float bw = 150 * S;
    float bx = x + w - 20 * S;
    if (mode == SlotMode::Load) {
      if (si.used) {
        bx -= bw; pill(c, bx, by, bw, bh, "EXCLUIR", base + 3, false, true, kHot);
        bx -= bw + 10 * S; pill(c, bx, by, bw, bh, "SUBSTITUIR", base + 2);
        bx -= bw + 10 * S; pill(c, bx, by, bw, bh, "CARREGAR", base + 1, true);
      } else {
        bx -= bw; pill(c, bx, by, bw, bh, "CRIAR", base + 4, true);
      }
    } else if (mode == SlotMode::New) {
      bx -= bw + 20 * S;
      pill(c, bx, by, bw + 20 * S, bh, si.used ? "SUBSTITUIR" : "CRIAR", base + (si.used ? 2 : 4), !si.used);
      if (si.used) pill(c, bx - bw - 10 * S, by, bw, bh, "EXCLUIR", base + 3, false, true, kHot);
    } else {  // save
      bx -= bw + 40 * S;
      pill(c, bx, by, bw + 40 * S, bh, active ? "SALVAR (ATUAL)" : "SALVAR AQUI", base + 5, active || !si.used);
    }
    // the whole row also selects (new game: pick the slot)
    if (mode == SlotMode::New) uiRects_.insert(uiRects_.begin(), {Vec4(x, ry, w, rh), base});
  }
}

// ------------------------------------------------------------------------------------------------ confirm
void Game::showConfirm(const std::string& title, const std::string& text, const std::string& yes, const std::string& no, std::function<void()> onYes) {
  confirm_.open = true;
  confirm_.title = title; confirm_.text = text; confirm_.yes = yes; confirm_.no = no;
  confirm_.onYes = std::move(onYes);
}

void Game::drawConfirm() {
  const float S = uiScale();
  Ctx c{ui_, uiRects_, S, pressedUi_, settings_.highContrast};
  ui_.rect(0, 0, screenW_, screenH_, rgba(0, 0, 0, 0.62f));
  float w = std::min(640.0f * S, screenW_ - 60 * S), h = 292 * S, x = (screenW_ - w) / 2, y = (screenH_ - h) / 2;
  ui_.glow(x, y + 8, w, h, 22 * S, 30 * S, rgba(0, 0, 0, 0.6f));
  ui_.rect(x, y, w, h, c.hc ? kPanelHc : kGlassHi, 20 * S, 1.2f * S, kLineHi);
  ui_.text(true, confirm_.title, x + 36 * S, y + 30 * S, 32 * S, kInk, Align::Left);
  auto lines = std::vector<std::string>();
  {
    std::string cur, word;
    auto flush = [&]() {
      if (word.empty()) return;
      std::string t = cur.empty() ? word : cur + " " + word;
      if (ui_.textWidth(false, t, 22 * S) > w - 72 * S && !cur.empty()) { lines.push_back(cur); cur = word; }
      else cur = t;
      word.clear();
    };
    for (char ch : confirm_.text) { if (ch == ' ') flush(); else word.push_back(ch); }
    flush();
    if (!cur.empty()) lines.push_back(cur);
  }
  float ty = y + 84 * S;
  for (const std::string& l : lines) { ui_.text(false, l, x + 36 * S, ty, 22 * S, dimCol(c), Align::Left); ty += 30 * S; }
  float bw = (w - 36 * 2 * S - 14 * S) / 2, by = y + h - 76 * S;
  pill(c, x + 36 * S, by, bw, 50 * S, confirm_.no, 2191);
  pill(c, x + 36 * S + bw + 14 * S, by, bw, 50 * S, confirm_.yes, 2190, true);
}

// ------------------------------------------------------------------------------------------------ pause menu
void Game::drawPauseMenu(float dt) {
  (void)dt;
  const float S = uiScale();
  uiRects_.clear();
  Ctx c{ui_, uiRects_, S, pressedUi_, settings_.highContrast};
  const float W = screenW_, H = screenH_;
  float fade = smoothstep(clamp(menuT_ / 0.25f, 0.0f, 1.0f));
  menuT_ += lastDt_;
  ui_.rect(0, 0, W, H, rgba(0.0f, 0.0f, 0.0f, (settings_.highContrast ? 0.78f : 0.58f) * fade));

  float px = W * 0.04f, py = H * 0.06f, pw = W * 0.92f, ph = H * 0.88f;
  // full-bleed: a dark veil that fades toward the right, no framed card
  ui_.hgradient(0, 0, W, H, rgba(0, 0, 0, 0.55f * fade), rgba(0.02f, 0.01f, 0.05f, (c.hc ? 0.85f : 0.35f) * fade), 0);

  if (settingsOnly_) {
    // opened from the main menu: just the settings
    ui_.text(true, "CONFIGURAÇÕES", px + 44 * S, py + 30 * S, 38 * S, kInk, Align::Left);
    pill(c, px + pw - 200 * S, py + 32 * S, 160 * S, 46 * S, "VOLTAR", 2205, true);
    drawSettingsTab(px + 44 * S, py + 100 * S, pw - 88 * S, ph - 120 * S);
    return;
  }

  // left column: title + tab list
  float lw = std::min(pw * 0.24f, 380.0f * S);
  ui_.text(true, "PAUSADO", px + 44 * S, py + 34 * S, 34 * S, kInk, Align::Left);
  ui_.text(false, upper(world_.cityName) + "  ·  " + locationName(player_.vehicle >= 0 ? vehicles_[player_.vehicle].pos : player_.pos, player_.indoors), px + 44 * S,
           py + 76 * S, 17 * S, dimCol(c), Align::Left);
  static const char* tabs[7] = {"MAPA", "JOGO", "INVENTÁRIO", "CÓDIGOS", "CONFIGURAÇÕES", "SALVAR", "MENU PRINCIPAL"};
  static const char* icons[7] = {"pin", "star", "bag", "bolt", "gear", "save", "close"};
  float ty = py + 126 * S, th = 66 * S;
  for (int i = 0; i < 7; ++i) {
    bool sel = pauseTab_ == i, pr = pressedUi_ == 2300 + i;
    float tw = ui_.textWidth(true, tabs[i], 24 * S) + 34 * S;
    if (sel) ui_.hgradient(px + 24 * S, ty + 6 * S, tw, th - 20 * S, rgba(0.97f, 0.55f, 0.72f), rgba(1.0f, 0.70f, 0.42f), 4 * S);
    else if (pr) ui_.rect(px + 24 * S, ty + 6 * S, tw, th - 20 * S, rgba(1, 1, 1, 0.12f), 4 * S);
    ui_.text(true, tabs[i], px + 24 * S + 17 * S, ty + (th - 8 * S) * 0.5f - 15 * S, 24 * S, sel ? rgba(0.08f, 0.04f, 0.10f) : (c.hc ? kInk : rgba(1, 1, 1, 0.92f)), Align::Left);
    (void)icons;
    uiRects_.push_back({Vec4(px + 24 * S, ty, lw - 12 * S, th - 8 * S), 2300 + i});
    ty += th;
  }
  pill(c, px + 40 * S, py + ph - 78 * S, lw - 44 * S, 50 * S, "CONTINUAR", 2399, true);
  ui_.rect(px + lw + 8 * S, py + 28 * S, 1.2f * S, ph - 56 * S, kLine);

  float cx = px + lw + 36 * S, cy = py + 30 * S, cw = pw - lw - 72 * S, chh = ph - 60 * S;
  switch (pauseTab_) {
    case 0: drawMapTab(cx, cy, cw, chh); break;
    case 1: drawGameTab(cx, cy, cw, chh); break;
    case 2: drawInventoryTab(cx, cy, cw, chh); break;
    case 3: drawCodesTab(cx, cy, cw, chh); break;
    case 4: drawSettingsTab(cx, cy, cw, chh); break;
    case 5: drawSaveTab(cx, cy, cw, chh); break;
    default: {
      ui_.text(true, "MENU PRINCIPAL", cx, cy, 36 * S, kInk, Align::Left);
      ui_.text(false, "Volte ao menu principal para iniciar um novo jogo, carregar outro save ou sair.", cx, cy + 56 * S, 22 * S, dimCol(c), Align::Left);
      pill(c, cx, cy + 130 * S, 420 * S, 56 * S, "SALVAR E VOLTAR AO MENU", 2910, true);
      pill(c, cx, cy + 204 * S, 420 * S, 56 * S, "VOLTAR SEM SALVAR", 2911, false, true, kWarn);
      break;
    }
  }
}

// ------------------------------------------------------------------------------------------------ tab: game summary
void Game::drawGameTab(float x, float y, float w, float h) {
  (void)h;
  const float S = uiScale();
  Ctx c{ui_, uiRects_, S, pressedUi_, settings_.highContrast};
  ui_.text(true, "JOGO", x, y, 36 * S, kInk, Align::Left);
  // key figures
  float colW = w / 4.0f;
  struct Fig { std::string label, value; };
  int hh = (int)(timeOfDay_), mm = (int)((timeOfDay_ - hh) * 60);
  char clk[16];
  std::snprintf(clk, sizeof(clk), "%02d:%02d", hh, mm);
  static const char* wn[4] = {"Automático", "Limpo", "Chuva", "Tempestade"};
  std::string wnow = rain_ > 0.6f ? "Tempestade" : (rain_ > 0.15f ? "Chuva" : (rain_ > 0.02f ? "Garoa" : "Limpo"));
  (void)wn;
  Fig figs[4] = {{"TEMPO DE JOGO", fmtClock((int)time_)}, {"DINHEIRO", fmtMoney(moneyCents_)}, {"HORA NA CIDADE", clk}, {"CLIMA", wnow}};
  for (int i = 0; i < 4; ++i) {
    ui_.text(false, figs[i].label, x + i * colW, y + 70 * S, 16 * S, dimCol(c), Align::Left);
    ui_.text(true, figs[i].value, x + i * colW, y + 94 * S, 34 * S, kInk, Align::Left);
  }
  ui_.rect(x, y + 156 * S, w, 1.2f * S, kLine);
  ui_.text(false, "CIDADE", x, y + 176 * S, 16 * S, dimCol(c), Align::Left);
  ui_.text(true, upper(world_.cityName) + "   ·   SEED " + std::to_string(worldSeed_), x, y + 200 * S, 28 * S, kInk, Align::Left);
  ui_.text(false, "NÍVEL " + std::to_string(level_) + "  ·  " + std::to_string(xp_) + "/" + std::to_string(xpForNext(level_)) + " XP  ·  " + std::to_string(jobsDone_) +
           " entregas  ·  " + fmtMoney(earned_) + " ganhos  ·  desconto " + std::to_string((int)std::lround(shopDiscount() * 100)) + "%", x, y + 238 * S, 19 * S, dimCol(c), Align::Left);
  ui_.rect(x, y + 282 * S, w, 1.2f * S, kLine);
  // milestones
  static const char* names[kPgCount] = {"Abasteceu um veículo", "Comprou em uma loja", "Consertou um veículo", "Nadou no mar", "Pegou uma arma",
                                        "Visitou as 4 lojas", "Despistou a polícia", "Dirigiu mais de 2 km"};
  int done = 0;
  for (int i = 0; i < kPgCount; ++i) done += (progress_ >> i) & 1u;
  ui_.text(false, "PROGRESSO", x, y + 302 * S, 16 * S, dimCol(c), Align::Left);
  ui_.text(true, std::to_string((int)std::lround(100.0f * done / kPgCount)) + "%", x, y + 324 * S, 34 * S, kInk, Align::Left);
  float bw = w * 0.5f;
  ui_.rect(x + 110 * S, y + 340 * S, bw, 6 * S, rgba(1, 1, 1, 0.12f), 3 * S);
  ui_.rect(x + 110 * S, y + 340 * S, bw * done / kPgCount, 6 * S, kAcc, 3 * S);
  float rowY = y + 392 * S;
  for (int i = 0; i < kPgCount; ++i) {
    int col = i % 2, row = i / 2;
    float rx = x + col * (w * 0.5f), ry = rowY + row * 50 * S;
    bool ok = (progress_ >> i) & 1u;
    ui_.circle(rx + 14 * S, ry + 16 * S, 9 * S, ok ? withAlpha(kOk, 0.9f) : rgba(1, 1, 1, 0.0f), 1.5f * S, ok ? kOk : kLineHi);
    ui_.text(false, names[i], rx + 38 * S, ry + 4 * S, 23 * S, ok ? kInk : dimCol(c), Align::Left);
  }
}

// ------------------------------------------------------------------------------------------------ tab: inventory
void Game::drawInventoryTab(float x, float y, float w, float h) {
  (void)h;
  const float S = uiScale();
  Ctx c{ui_, uiRects_, S, pressedUi_, settings_.highContrast};
  ui_.text(true, "INVENTÁRIO", x, y, 36 * S, kInk, Align::Left);
  ui_.text(true, fmtMoney(moneyCents_), x + w, y + 6 * S, 30 * S, kAcc, Align::Right);
  float colW = (w - 40 * S) / 2;
  // weapons
  ui_.text(false, "ARMAS", x, y + 66 * S, 16 * S, dimCol(c), Align::Left);
  float ry = y + 92 * S, rh = 58 * S;
  int shown = 0;
  for (int wid = 0; wid < kWeaponCount; ++wid) {
    if (!player_.owned[wid]) continue;
    const WeaponDef& wd = weaponDef(wid);
    bool eq = player_.weapon == wid;
    int id = 2650 + wid;
    ui_.rect(x, ry, colW, rh - 6 * S, pressedUi_ == id ? kRowHot : kRow, 12 * S, 1.2f * S, eq ? withAlpha(kAcc, 0.6f) : kLine);
    ui_.icon(wd.icon, x + 34 * S, ry + (rh - 6 * S) * 0.5f, 34 * S, eq ? kAcc : kInk);
    ui_.text(true, wd.name, x + 68 * S, ry + 7 * S, 22 * S, kInk, Align::Left);
    std::string sub = wd.magazine > 0 ? std::to_string(player_.mag[wid]) + " | " + std::to_string(player_.reserve[wid]) + " munição" : "Corpo a corpo";
    ui_.text(false, sub, x + 68 * S, ry + 31 * S, 17 * S, dimCol(c), Align::Left);
    ui_.text(true, eq ? "EQUIPADA" : "EQUIPAR", x + colW - 18 * S, ry + (rh - 6 * S) * 0.5f - 10 * S, 17 * S, eq ? kAcc : kDimHi, Align::Right);
    uiRects_.push_back({Vec4(x, ry, colW, rh - 6 * S), id});
    ry += rh;
    if (++shown >= 9) break;
  }
  // items
  float ix = x + colW + 40 * S;
  ui_.text(false, "ITENS", ix, y + 66 * S, 16 * S, dimCol(c), Align::Left);
  ry = y + 92 * S;
  bool any = false;
  for (int i = 1; i < kItemCount; ++i) {
    if (inventory_[i] <= 0) continue;
    any = true;
    const ItemDef& d = itemDef(i);
    int id = 2600 + i;
    ui_.rect(ix, ry, colW, rh - 6 * S, pressedUi_ == id ? kRowHot : kRow, 12 * S, 1.2f * S, kLine);
    if (d.art) ui_.art(d.art, ix + 8 * S, ry + 4 * S, 44 * S, 44 * S, kWhite, 8 * S);
    else ui_.icon(d.icon, ix + 30 * S, ry + (rh - 6 * S) * 0.5f, 30 * S, kInk);
    ui_.text(true, d.name, ix + 68 * S, ry + 7 * S, 22 * S, kInk, Align::Left);
    std::string eff;
    if (d.health > 0) eff += "+" + fmtFloat(d.health, 0) + " vida  ";
    if (d.stamina > 0) eff += "+" + fmtFloat(d.stamina, 0) + " fôlego  ";
    if (d.runBoostSecs > 0) eff += "corrida livre";
    ui_.text(false, eff, ix + 68 * S, ry + 31 * S, 17 * S, dimCol(c), Align::Left);
    ui_.text(true, "x" + std::to_string(inventory_[i]), ix + colW - 100 * S, ry + (rh - 6 * S) * 0.5f - 12 * S, 22 * S, kInk, Align::Right);
    ui_.text(true, "USAR", ix + colW - 18 * S, ry + (rh - 6 * S) * 0.5f - 10 * S, 17 * S, kAcc, Align::Right);
    uiRects_.push_back({Vec4(ix, ry, colW, rh - 6 * S), id});
    ry += rh;
  }
  if (!any) ui_.text(false, "Nenhum item. Compre nas lojas da cidade.", ix, ry + 8 * S, 21 * S, dimCol(c), Align::Left);
}

// ------------------------------------------------------------------------------------------------ tab: codes
void Game::drawCodesTab(float x, float y, float w, float h) {
  (void)h;
  const float S = uiScale();
  Ctx c{ui_, uiRects_, S, pressedUi_, settings_.highContrast};
  ui_.text(true, "CÓDIGOS", x, y, 36 * S, kInk, Align::Left);
  ui_.text(false, "Atalhos de teste. Cada código muda o jogo de verdade e confirma na tela.", x, y + 54 * S, 20 * S, dimCol(c), Align::Left);
  struct Code { const char* name; const char* desc; const char* icon; };
  static const Code codes[10] = {
      {"RECUPERAR VIDA", "Vida e fôlego no máximo", "heart"},      {"DAR ARMAS", "Todas as armas, com munição", "crosshair"},
      {"MUNIÇÃO", "Reabastece pentes e reservas", "ammo"},        {"DINHEIRO DE TESTE", "+ R$ 5.000,00", "coin"},
      {"REMOVER PROCURADO", "Zera as estrelas e dispensa a polícia", "star"}, {"ADICIONAR PROCURADO", "Sobe um nível de procurado", "star"},
      {"REPARAR VEÍCULO", "Integridade 100% no veículo atual", "wrench"}, {"ENCHER TANQUE", "Tanque cheio no veículo atual", "fuel"},
      {"MUDAR CLIMA", "Alterna limpo, chuva e tempestade", "bolt"}, {"AVANÇAR HORA", "+ 3 horas no relógio da cidade", "camera"}};
  float colW = (w - 28 * S) / 2, rh = 86 * S;
  for (int i = 0; i < 10; ++i) {
    int col = i % 2, row = i / 2;
    float rx = x + col * (colW + 28 * S), ry = y + 104 * S + row * rh;
    int id = 2500 + i;
    bool pr = pressedUi_ == id;
    ui_.rect(rx, ry, colW, rh - 10 * S, pr ? kRowHot : kRow, 14 * S, 1.2f * S, kLine);
    ui_.circle(rx + 40 * S, ry + (rh - 10 * S) * 0.5f, 24 * S, rgba(1, 1, 1, 0.07f), 1.2f * S, kLine);
    ui_.icon(codes[i].icon, rx + 40 * S, ry + (rh - 10 * S) * 0.5f, 28 * S, kAcc);
    ui_.text(true, codes[i].name, rx + 82 * S, ry + 12 * S, 23 * S, kInk, Align::Left);
    ui_.text(false, codes[i].desc, rx + 82 * S, ry + 42 * S, 18 * S, dimCol(c), Align::Left);
    uiRects_.push_back({Vec4(rx, ry, colW, rh - 10 * S), id});
  }
  // visible confirmation of the last code
  if (codeStatusT_ > 0) {
    float a = clamp(codeStatusT_ / 0.6f, 0.0f, 1.0f);
    float bw = std::min(w, 760.0f * S), bx = x + (w - bw) / 2, by = y + 104 * S + 5 * rh + 6 * S;
    ui_.rect(bx, by, bw, 52 * S, withAlpha(kOk, 0.16f * a), 26 * S, 1.2f * S, withAlpha(kOk, 0.7f * a));
    ui_.text(true, codeStatus_, bx + bw / 2, by + 13 * S, 22 * S, withAlpha(kOk, a), Align::Center);
    codeStatusT_ -= lastDt_;
  }
}

// ------------------------------------------------------------------------------------------------ tab: save
void Game::drawSaveTab(float x, float y, float w, float h) {
  const float S = uiScale();
  Ctx c{ui_, uiRects_, S, pressedUi_, settings_.highContrast};
  ui_.text(true, "SALVAR", x, y, 36 * S, kInk, Align::Left);
  ui_.text(false, "Espaço ativo: " + std::to_string(activeSlot_) + "   ·   o jogo também salva sozinho a cada poucos segundos", x, y + 54 * S, 20 * S, dimCol(c), Align::Left);
  pill(c, x + w - 260 * S, y + 6 * S, 260 * S, 52 * S, "SALVAR AGORA", 2900, true);
  drawSlotList(x, y + 104 * S, w, h - 104 * S, SlotMode::Save);
}

// ------------------------------------------------------------------------------------------------ tab: settings
void Game::drawSettingsTab(float x, float y, float w, float h) {
  (void)h;
  const float S = uiScale();
  Ctx c{ui_, uiRects_, S, pressedUi_, settings_.highContrast};
  static const char* names[5] = {"GRÁFICOS", "ÁUDIO", "CONTROLES", "JOGABILIDADE", "ACESSIBILIDADE"};
  float tabW = w / 5.0f;
  for (int i = 0; i < 5; ++i) {
    bool sel = settingsTab_ == i;
    float tx = x + i * tabW;
    ui_.text(true, names[i], tx + tabW * 0.5f, y + 14 * S, 20 * S, sel ? kInk : dimCol(c), Align::Center);
    ui_.rect(tx + 14 * S, y + 52 * S, tabW - 28 * S, sel ? 3 * S : 1.2f * S, sel ? kAcc : kLine, 1.5f * S);
    uiRects_.push_back({Vec4(tx, y, tabW, 60 * S), 2400 + i});
  }
  float ry = y + 82 * S, rh = 68 * S;
  float rw = w;
  auto label = [&](const char* name, const char* desc) {
    ui_.text(true, name, x + 22 * S, ry + 8 * S, 24 * S, kInk, Align::Left);
    if (desc && *desc) ui_.text(false, desc, x + 22 * S, ry + 36 * S, 16 * S, dimCol(c), Align::Left);
  };
  auto rowBg = [&](int id) { ui_.rect(x, ry, rw, rh - 8 * S, pressedUi_ == id ? kRowHot : kRow, 14 * S, 1.0f * S, kLine); };
  auto toggle = [&](const char* name, const char* desc, bool on, int id) {
    rowBg(id);
    label(name, desc);
    float sw = 74 * S, sh = 38 * S, sx = x + rw - sw - 24 * S, sy = ry + (rh - 8 * S) * 0.5f - sh * 0.5f;
    ui_.rect(sx, sy, sw, sh, on ? withAlpha(kAcc, 0.9f) : rgba(1, 1, 1, 0.14f), sh * 0.5f);
    ui_.circle(sx + (on ? sw - sh * 0.5f : sh * 0.5f), sy + sh * 0.5f, sh * 0.5f - 4 * S, on ? rgba(0.05f, 0.06f, 0.08f) : kInk);
    uiRects_.push_back({Vec4(x, ry, rw, rh - 8 * S), id});
    ry += rh;
  };
  auto step = [&](const char* name, const char* desc, const std::string& value, int id) {
    rowBg(id);
    label(name, desc);
    float vw = std::max(150.0f * S, ui_.textWidth(true, value, 21 * S) + 44 * S);
    float vx = x + rw - vw - 24 * S;
    ui_.rect(vx, ry + (rh - 8 * S) * 0.5f - 19 * S, vw, 38 * S, rgba(1, 1, 1, 0.07f), 19 * S, 1.2f * S, kLineHi);
    ui_.text(true, value, vx + vw * 0.5f, ry + (rh - 8 * S) * 0.5f - 11 * S, 21 * S, kAcc, Align::Center);
    uiRects_.push_back({Vec4(x, ry, rw, rh - 8 * S), id});
    ry += rh;
  };
  auto slider = [&](const char* name, const char* desc, float v, float lo, float hi, int id, const std::string& value) {
    rowBg(id);
    label(name, desc);
    float tx = x + rw * 0.46f, tw = rw * 0.38f, ty = ry + (rh - 8 * S) * 0.5f;
    float t = clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
    ui_.rect(tx, ty - 3 * S, tw, 6 * S, rgba(1, 1, 1, 0.16f), 3 * S);
    ui_.rect(tx, ty - 3 * S, tw * t, 6 * S, kAcc, 3 * S);
    ui_.circle(tx + tw * t, ty, 12 * S, kInk, 2 * S, kAcc);
    ui_.text(true, value, x + rw - 24 * S, ty - 12 * S, 21 * S, kInk, Align::Right);
    uiRects_.push_back({Vec4(tx - 20 * S, ry, tw + 40 * S, rh - 8 * S), id});
    ry += rh;
  };
  const Settings& s = settings_;
  auto pct = [](float v) { return std::to_string((int)std::lround(v * 100)) + "%"; };
  switch (settingsTab_) {
    case 0: {
      static const char* q[4] = {"BAIXO", "MÉDIO", "ALTO", "ULTRA"};
      step("Qualidade gráfica", "Resolução, sombras, luzes, distância e animação", q[clamp(s.quality, 0, 3)], 3000);
      toggle("Resolução dinâmica", "Reduz a resolução quando o quadro passa do orçamento", s.dynamicRes, 3001);
      toggle("Sombras do sol", "Cascatas de sombra em tempo real", s.shadows, 3002);
      slider("Distância de visão", "Props, pedestres, veículos e troca de LOD", s.drawDistance, 0.6f, 1.5f, 1002, pct(s.drawDistance));
      toggle("Brilho do bloom", "Halo em luzes, faróis e letreiros", s.bloom, 3003);
      toggle("Reflexos em tela", "Chão molhado reflete carros, prédios e luzes", s.reflections, 3091);
      toggle("Desfoque de movimento", "Suaviza giros rápidos da câmera (Alto / Ultra)", s.motionBlur, 3092);
      toggle("Efeitos de chuva", "Gotas e respingos (o clima continua afetando a luz)", s.weatherFx, 3004);
      slider("Exposição", "Brilho geral da imagem", s.brightness, 0.7f, 1.4f, 1003, pct(s.brightness));
      toggle("Mostrar FPS", "Contador e métricas de desempenho", s.showFps, 3005);
      break;
    }
    case 1: {
      slider("Volume geral", "", s.master, 0.0f, 1.0f, 1010, pct(s.master));
      slider("Efeitos", "Tiros, passos, impactos, motores", s.sfx, 0.0f, 1.0f, 1011, pct(s.sfx));
      slider("Ambiente", "Mar, chuva, trovões, sirenes", s.ambience, 0.0f, 1.0f, 1012, pct(s.ambience));
      toggle("Silenciar tudo", "", s.muted, 3010);
      break;
    }
    case 2: {
      slider("Sensibilidade da câmera", "Arrastar para girar", s.sensitivity, 0.4f, 2.0f, 1020, fmtFloat(s.sensitivity, 2) + "x");
      toggle("Inverter eixo Y", "Terceira pessoa", s.invertY, 3020);
      toggle("Câmera isométrica", "Top Down vira vista isométrica", s.isometric, 3090);
      slider("Tamanho dos controles", "Botões e joystick", s.hudScale, 0.75f, 1.35f, 1021, pct(s.hudScale));
      slider("Opacidade dos controles", "Quanto os botões aparecem sobre a cena", s.hudOpacity, 0.3f, 1.0f, 1022, pct(s.hudOpacity));
      toggle("Mira assistida", "Ajuda a acertar alvos próximos", s.aimAssist, 3021);
      break;
    }
    case 3: {
      static const char* wn[4] = {"AUTOMÁTICO", "LIMPO", "CHUVA", "TEMPESTADE"};
      static const char* dn[3] = {"PARADO", "NORMAL", "RÁPIDO"};
      step("Clima", "Automático alterna sol e chuva sozinho", wn[clamp(s.weatherMode, 0, 3)], 3030);
      step("Ciclo do dia", "Velocidade do relógio da cidade", dn[clamp(s.dayCycle, 0, 2)], 3031);
      step("Hora do dia", "Toque para avançar 3 horas", [&]() { char b[16]; int hh = (int)timeOfDay_; std::snprintf(b, sizeof(b), "%02d:%02d", hh, (int)((timeOfDay_ - hh) * 60)); return std::string(b); }(), 3035);
      toggle("Minimapa", "Radar no canto da tela", s.showMinimap, 3032);
      toggle("Dicas na tela", "Avisos e orientações", s.hints, 3033);
      toggle("Salvamento automático", "Salva o espaço ativo periodicamente", s.autosave, 3034);
      break;
    }
    default: {
      slider("Tamanho do texto", "Todos os textos da interface", s.textScale, 0.85f, 1.3f, 1030, pct(s.textScale));
      toggle("Alto contraste", "Painéis opacos e texto mais claro", s.highContrast, 3040);
      toggle("Reduzir movimento", "Sem tremor de câmera, vento e zoom suaves", s.reduceMotion, 3041);
      toggle("Reduzir flashes", "Raios e clarões mais discretos", s.reduceFlashes, 3042);
      break;
    }
  }
}

// ------------------------------------------------------------------------------------------------ tab: full map
void Game::drawMapTab(float x, float y, float w, float h) {
  const float S = uiScale();
  Ctx c{ui_, uiRects_, S, pressedUi_, settings_.highContrast};
  float side = std::min(h, w - 330 * S);
  float mx = x, my = y + (h - side) * 0.5f;
  float E = mapExtent_;
  Vec2 pp = player_.vehicle >= 0 ? vehicles_[player_.vehicle].pos : player_.pos;
  float fit = side / (2.0f * E);                       // pixels per metre showing the whole city
  if (mapZoom_ <= 0) { mapZoom_ = clamp(side / 150.0f, fit, 10.0f); mapCenter_ = pp; }
  mapZoom_ = clamp(mapZoom_, fit, 10.0f);
  // keep the view inside the map
  float halfView = side * 0.5f / mapZoom_;
  mapCenter_.x = clamp(mapCenter_.x, -E + halfView, E - halfView);
  mapCenter_.y = clamp(mapCenter_.y, -E + halfView, E - halfView);
  if (halfView >= E) mapCenter_ = {0, 0};
  float k = mapZoom_;
  float uvScale = side / (k * 2.0f * E);
  ui_.rect(mx - 2 * S, my - 2 * S, side + 4 * S, side + 4 * S, kLine, 16 * S);
  ui_.map(mapTex_, 0.5f + mapCenter_.x / (2 * E), 0.5f + mapCenter_.y / (2 * E), uvScale, 0.0f, mx, my, side, side, 14 * S, kWhite);
  mapRect_ = Vec4(mx, my, side, side);
  uiRects_.push_back({Vec4(mx, my, side, side), 2800});
  auto toScreen = [&](Vec2 wp) { return Vec2{mx + side * 0.5f + (wp.x - mapCenter_.x) * k, my + side * 0.5f + (wp.y - mapCenter_.y) * k}; };
  auto inside = [&](Vec2 s, float m) { return s.x > mx + m && s.x < mx + side - m && s.y > my + m && s.y < my + side - m; };
  // district / street labels when zoomed out enough to read
  if (k < 3.2f) {
    for (const auto& b : world_.blocks) {
      Vec2 sc = toScreen({b.first.cx(), b.first.cz()});
      if (!inside(sc, 30 * S)) continue;
      ui_.text(true, upper(kDistrictNames[(int)b.second]), sc.x, sc.y - 9 * S, 13 * S, rgba(1, 1, 1, 0.34f), Align::Center);
    }
  }
  auto marker = [&](Vec2 wp, const char* icon, Color col, float sz, const std::string& text = "") {
    Vec2 sc = toScreen(wp);
    if (!inside(sc, sz * 0.5f)) return;
    ui_.icon(icon, sc.x, sc.y, sz, col);
    if (!text.empty() && k > 1.6f) ui_.text(false, text, sc.x, sc.y + sz * 0.55f, 14 * S, rgba(1, 1, 1, 0.78f), Align::Center, rgba(0, 0, 0, 0.8f), 0.14f);
  };
  for (const ShopDef& sh : world_.shops) marker({sh.door.x, sh.door.z}, sh.kind == ShopKind::Conveniencia ? "fuel" : "cart", kAcc, 30 * S, sh.name);
  marker({world_.poiGas.x, world_.poiGas.z}, "fuel", kOk, 30 * S, "Posto");
  marker({world_.poiWorkshop.x, world_.poiWorkshop.z}, "wrench", kAcc, 30 * S, "Oficina");
  for (const Vehicle& v : vehicles_)
    if (!v.police && !v.despawn && !v.traffic && player_.vehicle != v.id) marker(v.pos, "car", kInk, 24 * S);
  for (const Pickup& pk : pickups_)
    if (pk.active) marker({pk.pos.x, pk.pos.z}, weaponDef(pk.weapon).icon, kWarn, 22 * S);
  if (wanted_ > 0)
    for (const Npc& n : npcs_)
      if (n.police && !n.despawn && n.state != NpcState::Dead) marker(n.pos, "dot", std::fmod(realTime_ * 2.6f, 1.0f) < 0.5f ? kHot : rgba(0.5f, 0.7f, 1.0f), 16 * S);
  if (waypoint_.active) marker({waypoint_.pos.x, waypoint_.pos.z}, "pin", kHot, 38 * S, waypoint_.name);
  // player
  {
    Vec2 sc = toScreen(pp);
    float heading = player_.vehicle >= 0 ? vehicles_[player_.vehicle].yaw : player_.yaw;
    if (inside(sc, 10 * S)) {
      ui_.arc(sc.x, sc.y, 0, 24 * S, heading - 0.55f, heading + 0.55f, rgba(1, 1, 1, 0.9f));
      ui_.circle(sc.x, sc.y, 8 * S, kAcc, 2.0f * S, rgba(0.05f, 0.06f, 0.08f, 0.95f));
    }
  }
  // right column: info, controls, legend
  float rx = mx + side + 34 * S, rw = x + w - rx;
  ui_.text(true, upper(world_.cityName), rx, my + 4 * S, 30 * S, kInk, Align::Left);
  ui_.text(false, "Seed " + std::to_string(worldSeed_), rx, my + 42 * S, 18 * S, dimCol(c), Align::Left);
  ui_.text(false, "Você está em", rx, my + 84 * S, 16 * S, dimCol(c), Align::Left);
  ui_.text(true, locationName(pp, player_.indoors), rx, my + 106 * S, 24 * S, kInk, Align::Left);
  float by = my + 170 * S;
  float bs = 54 * S;
  pill(c, rx, by, bs, bs, "+", 2801);
  pill(c, rx + bs + 10 * S, by, bs, bs, "-", 2802);
  pill(c, rx + (bs + 10 * S) * 2, by, rw - (bs + 10 * S) * 2, bs, "CENTRALIZAR", 2803);
  if (waypoint_.active) pill(c, rx, by + bs + 10 * S, rw, 46 * S, "REMOVER MARCADOR", 2804, false, true, kHot);
  float ly = by + bs + (waypoint_.active ? 76 : 28) * S;
  struct L { const char* icon; const char* text; Color col; };
  const L legend[] = {{"cart", "Loja", kAcc}, {"fuel", "Posto", kOk}, {"wrench", "Oficina", kAcc}, {"car", "Veículo", kInk}, {"pin", "Marcador", kHot}};
  for (const L& l : legend) {
    ui_.icon(l.icon, rx + 14 * S, ly + 14 * S, 24 * S, l.col);
    ui_.text(false, l.text, rx + 40 * S, ly + 3 * S, 18 * S, dimCol(c), Align::Left);
    ly += 34 * S;
  }
  ui_.text(false, "Arraste para mover  ·  pinça ou +/− para zoom", rx, my + side - 52 * S, 15 * S, kFaint, Align::Left);
  ui_.text(false, "Toque no mapa para marcar um destino", rx, my + side - 30 * S, 15 * S, kFaint, Align::Left);
}

// ------------------------------------------------------------------------------------------------ pointers
void Game::handleMenuPointers(const InputFrame& in) {
  const float S = uiScale();
  auto hit = [&](Vec2 p) {
    for (auto it = uiRects_.rbegin(); it != uiRects_.rend(); ++it) {
      const Vec4& r = it->first;
      if (confirm_.open && it->second != 2190 && it->second != 2191) continue;
      if (p.x >= r.x && p.x <= r.x + r.z && p.y >= r.y && p.y <= r.y + r.w) return it->second;
    }
    return -1;
  };
  // pinch zoom on the map
  if (menu_ == MenuState::Pause && pauseTab_ == 0 && !settingsOnly_ && !confirm_.open) {
    std::vector<const UiPointer*> dn;
    for (const UiPointer& p : in.ui)
      if (p.down && !p.released && p.pos.x >= mapRect_.x && p.pos.x <= mapRect_.x + mapRect_.z && p.pos.y >= mapRect_.y && p.pos.y <= mapRect_.y + mapRect_.w) dn.push_back(&p);
    if (dn.size() >= 2) {
      float d = (dn[0]->pos - dn[1]->pos).length();
      if (mapPinchPrev_ > 0) mapZoom_ *= clamp(d / mapPinchPrev_, 0.8f, 1.25f);
      mapPinchPrev_ = d;
      mapDragging_ = false;
      mapMoved_ = true;
    } else mapPinchPrev_ = 0;
  }
  for (const UiPointer& p : in.ui) {
    if (p.pressed) {
      pressedUi_ = hit(p.pos);
      if (pressedUi_ >= 1000 && pressedUi_ < 1100) {
        activeSlider_ = pressedUi_;
        for (const auto& pr : uiRects_)
          if (pr.second == pressedUi_) applySettingSlider(pressedUi_, clamp((p.pos.x - (pr.first.x + 20 * S)) / std::max(1.0f, pr.first.z - 40 * S), 0.0f, 1.0f));
      }
      if (pressedUi_ == 2800) { mapDragging_ = true; mapDragPrev_ = p.pos; mapMoved_ = false; }
    } else if (p.down && !p.released) {
      if (activeSlider_ >= 0) {
        for (const auto& pr : uiRects_)
          if (pr.second == activeSlider_) applySettingSlider(activeSlider_, clamp((p.pos.x - (pr.first.x + 20 * S)) / std::max(1.0f, pr.first.z - 40 * S), 0.0f, 1.0f));
      } else if (mapDragging_ && mapPinchPrev_ <= 0) {
        Vec2 d = p.pos - mapDragPrev_;
        mapCenter_ -= d / std::max(0.01f, mapZoom_);
        mapDragPrev_ = p.pos;
        if ((p.pos - p.start).length() > 14 * S) mapMoved_ = true;
      }
    }
    if (p.released) {
      if (activeSlider_ >= 0) {
        activeSlider_ = -1;
        pressedUi_ = -1;
        applySettings();
        saveSettings();
        continue;
      }
      int id = hit(p.pos);
      bool moved = (p.pos - p.start).length() > 18 * S;
      if (pressedUi_ == 2800 && id == 2800) {
        mapDragging_ = false;
        if (!moved && !mapMoved_) {
          // tap on the map: place the destination marker there
          float side = mapRect_.z, k = mapZoom_;
          Vec2 wp{mapCenter_.x + (p.pos.x - (mapRect_.x + side * 0.5f)) / k, mapCenter_.y + (p.pos.y - (mapRect_.y + side * 0.5f)) / k};
          waypoint_.active = true;
          waypoint_.pos = {wp.x, world_.heightAt(wp.x, wp.y), wp.y};
          waypoint_.name = locationName(wp, false);
          toast("Marcador: " + waypoint_.name, "pin");
        }
      } else if (id >= 0 && id == pressedUi_ && !moved) {
        menuAction(id);
      }
      mapDragging_ = false;
      pressedUi_ = -1;
    }
  }
}

// ------------------------------------------------------------------------------------------------ actions
void Game::menuAction(int id) {
  if (confirm_.open) {
    if (id == 2190) { auto f = std::move(confirm_.onYes); confirm_.open = false; if (f) f(); }
    else if (id == 2191) confirm_.open = false;
    return;
  }
  // ---- main menu
  if (id >= 2000 && id <= 2004) {
    if (id == 2000) { int ls = latestSlot(); if (ls) loadSlot(ls); }
    else if (id == 2001) { slotMode_ = SlotMode::New; menu_ = MenuState::Slots; refreshSlots(); menuT_ = 0; }
    else if (id == 2002) { slotMode_ = SlotMode::Load; menu_ = MenuState::Slots; refreshSlots(); menuT_ = 0; }
    else if (id == 2003) { settingsOnly_ = true; settingsTab_ = 0; pauseTab_ = 4; menu_ = MenuState::Pause; }
    else quit_ = true;
    return;
  }
  if (id == 2200) { menu_ = MenuState::Main; menuT_ = 0.4f; return; }
  if (id == 2205) { saveSettings(); settingsOnly_ = false; menu_ = MenuState::Main; menuT_ = 0.4f; return; }
  // ---- slots
  if (id >= 2100 && id < 2100 + kSlots * 10) {
    int slot = (id - 2100) / 10 + 1, k = (id - 2100) % 10;
    SlotInfo si = slots_[slot - 1];
    if (k == 1) { loadSlot(slot); }
    else if (k == 2) {
      showConfirm("Substituir o espaço " + std::to_string(slot) + "?", "O jogo salvo em " + si.city + " será apagado e uma cidade nova será gerada.", "SUBSTITUIR", "CANCELAR",
                  [this, slot]() { startNewGame(slot); });
    } else if (k == 3) {
      showConfirm("Excluir o espaço " + std::to_string(slot) + "?", "Este save será apagado permanentemente.", "EXCLUIR", "CANCELAR", [this, slot]() { deleteSlot(slot); });
    } else if (k == 4) { startNewGame(slot); }
    else if (k == 5) {
      auto doSave = [this, slot]() {
        if (saveToSlot(slot)) { codeStatus_ = ""; toast("Jogo salvo no espaço " + std::to_string(slot), "save"); }
        else toast("Não foi possível salvar", "close", rgba(1.0f, 0.5f, 0.45f));
      };
      if (si.used && slot != activeSlot_) showConfirm("Sobrescrever o espaço " + std::to_string(slot) + "?", "Ele guarda outro jogo em " + si.city + ".", "SOBRESCREVER", "CANCELAR", doSave);
      else doSave();
    } else if (k == 0 && slotMode_ == SlotMode::New) {
      if (si.used) showConfirm("Substituir o espaço " + std::to_string(slot) + "?", "O jogo salvo em " + si.city + " será apagado e uma cidade nova será gerada.", "SUBSTITUIR", "CANCELAR",
                               [this, slot]() { startNewGame(slot); });
      else startNewGame(slot);
    }
    return;
  }
  // ---- pause menu
  if (id >= 2300 && id <= 2306) { pauseTab_ = id - 2300; refreshSlots(); if (pauseTab_ == 0) mapZoom_ = 0; return; }
  if (id == 2399) { menu_ = MenuState::None; saveSettings(); return; }
  if (id == 2900) {
    if (saveToSlot(activeSlot_)) toast("Jogo salvo no espaço " + std::to_string(activeSlot_), "save");
    return;
  }
  if (id == 2910) { if (saveGame()) {} enterMainMenu(); return; }
  if (id == 2911) {
    showConfirm("Voltar sem salvar?", "O progresso desde o último salvamento será perdido.", "VOLTAR AO MENU", "CANCELAR", [this]() { enterMainMenu(); });
    return;
  }
  if (id >= 2400 && id <= 2404) { settingsTab_ = id - 2400; return; }
  if (id >= 2500 && id < 2510) { runCode(id - 2500); return; }
  if (id >= 2600 && id < 2650) { useItem(id - 2600); return; }
  if (id >= 2650 && id < 2700) {
    int w = id - 2650;
    if (w < kWeaponCount && player_.owned[w] && w != player_.weapon) { equipWeapon(w); toast(std::string("Equipado: ") + weaponDef(w).name, weaponDef(w).icon); }
    return;
  }
  // ---- map
  if (id == 2801) { mapZoom_ *= 1.4f; return; }
  if (id == 2802) { mapZoom_ /= 1.4f; return; }
  if (id == 2803) { mapZoom_ = 0; return; }
  if (id == 2804) { waypoint_.active = false; toast("Marcador removido", "pin"); return; }
  // ---- settings
  if (id >= 3000 && id < 3100) { applySettingStep(id); return; }
}

void Game::applySettingStep(int id) {
  Settings& s = settings_;
  switch (id) {
    case 3000: s.quality = (s.quality + 1) % 4; break;
    case 3001: s.dynamicRes = !s.dynamicRes; break;
    case 3002: s.shadows = !s.shadows; break;
    case 3003: s.bloom = !s.bloom; break;
    case 3091: s.reflections = !s.reflections; break;
    case 3092: s.motionBlur = !s.motionBlur; break;
    case 3004: s.weatherFx = !s.weatherFx; break;
    case 3005: s.showFps = !s.showFps; break;
    case 3010: s.muted = !s.muted; break;
    case 3020: s.invertY = !s.invertY; break;
    case 3090: s.isometric = !s.isometric; break;
    case 3021: s.aimAssist = !s.aimAssist; break;
    case 3030: s.weatherMode = (s.weatherMode + 1) % 4; break;
    case 3031: s.dayCycle = (s.dayCycle + 1) % 3; dayRateOverridden_ = false; break;
    case 3032: s.showMinimap = !s.showMinimap; break;
    case 3033: s.hints = !s.hints; break;
    case 3034: s.autosave = !s.autosave; break;
    case 3035: timeOfDay_ = std::fmod(timeOfDay_ + 3.0f, 24.0f); break;
    case 3040: s.highContrast = !s.highContrast; break;
    case 3041: s.reduceMotion = !s.reduceMotion; break;
    case 3042: s.reduceFlashes = !s.reduceFlashes; break;
    default: break;
  }
  applySettings();
  saveSettings();
}

void Game::applySettingSlider(int id, float t) {
  Settings& s = settings_;
  switch (id) {
    case 1002: s.drawDistance = 0.6f + t * 0.9f; break;
    case 1003: s.brightness = 0.7f + t * 0.7f; break;
    case 1010: s.master = t; break;
    case 1011: s.sfx = t; break;
    case 1012: s.ambience = t; break;
    case 1020: s.sensitivity = 0.4f + t * 1.6f; break;
    case 1021: s.hudScale = 0.75f + t * 0.6f; break;
    case 1022: s.hudOpacity = 0.3f + t * 0.7f; break;
    case 1030: s.textScale = 0.85f + t * 0.45f; break;
    default: break;
  }
  if (id >= 1010 && id <= 1012) applyAudioSettings();
}

// ------------------------------------------------------------------------------------------------ codes (real effects)
void Game::runCode(int code) {
  std::string msg;
  switch (code) {
    case 0:
      player_.health = 100; player_.stamina = 100; player_.dead = false; player_.down = false;
      msg = "Vida e fôlego recuperados";
      break;
    case 1:
      for (int w = 1; w < kWeaponCount; ++w) giveWeapon(w, 90);
      msg = "Todas as armas adicionadas ao inventário";
      break;
    case 2: {
      int n = 0;
      for (int w = 1; w < kWeaponCount; ++w)
        if (player_.owned[w] && weaponDef(w).magazine > 0) { player_.mag[w] = weaponDef(w).magazine; player_.reserve[w] = weaponDef(w).reserveMax; ++n; }
      msg = n ? "Munição reabastecida (" + std::to_string(n) + " armas)" : "Você não tem armas de fogo";
      break;
    }
    case 3:
      moneyCents_ += 500000; moneyShow_ = 4.0f;
      msg = "+ R$ 5.000,00  ·  saldo " + fmtMoney(moneyCents_);
      break;
    case 4:
      wanted_ = 0; wantedHeat_ = 0; sinceSeen_ = 1e9f; evadeT_ = 0;
      for (Npc& n : npcs_) if (n.police) n.despawn = true;
      for (Vehicle& v : vehicles_) if (v.police && v.occupant < 0) { v.despawn = true; v.pos = {9999, 9999}; }
      msg = "Procurado removido";
      break;
    case 5: {
      static const float heat[3] = {0.5f, 3.0f, 6.0f};
      if (wanted_ >= 3) { msg = "Procurado já está no nível máximo"; break; }
      lastReportT_ = -100.0f;   // a fresh incident: it must not merge with the previous report
      reportCrime(player_.pos, std::max(0.1f, heat[wanted_] - wantedHeat_ + 0.05f), true);
      msg = "Procurado aumentado para o nível " + std::to_string(std::min(3, wanted_ + 1));
      break;
    }
    case 6: case 7: {
      int vi = player_.vehicle >= 0 ? player_.vehicle : nearestVehicleTo(player_.pos, 40.0f);
      if (vi < 0) { msg = "Nenhum veículo por perto"; break; }
      Vehicle& v = vehicles_[vi];
      if (code == 6) { v.health = 100; v.wrecked = false; v.smokeTimer = 0; msg = std::string("Veículo reparado: ") + vehicleDef(v.model).name; }
      else { v.fuel = vehicleDef(v.model).fuelCap; msg = std::string("Tanque cheio: ") + vehicleDef(v.model).name; }
      break;
    }
    case 8: {
      settings_.weatherMode = settings_.weatherMode == 2 ? 3 : (settings_.weatherMode == 3 ? 1 : 2);
      applySettings();
      saveSettings();
      static const char* n[4] = {"automático", "limpo", "chuva", "tempestade"};
      msg = std::string("Clima: ") + n[settings_.weatherMode];
      break;
    }
    default:
      timeOfDay_ = std::fmod(timeOfDay_ + 3.0f, 24.0f);
      msg = "Hora avançada para " + fmtFloat(timeOfDay_, 0) + "h";
      break;
  }
  codeStatus_ = "OK  -  " + msg;
  codeStatusT_ = 3.5f;
  toast(msg, code == 3 ? "coin" : (code == 4 || code == 5 ? "star" : "bolt"));
}

}  // namespace gtabr
