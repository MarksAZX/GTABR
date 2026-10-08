// Save slots and settings. Every slot is a small key=value text file written atomically in the app's private storage:
// the city seed (the whole city is rebuilt from it on load), player state, money, inventory, weapons and ammo, vehicles,
// weather/time, playtime and progress. Settings live in their own global file.
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

#include "../core/fileio.h"
#include "../core/log.h"
#include "game.h"

namespace gtabr {

namespace {
using KV = std::unordered_map<std::string, std::string>;
KV parseKv(const std::string& text) {
  KV kv;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    kv[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return kv;
}
float numOf(const KV& kv, const char* k, float def) {
  auto it = kv.find(k);
  return it == kv.end() ? def : (float)std::atof(it->second.c_str());
}
std::string strOf(const KV& kv, const char* k) {
  auto it = kv.find(k);
  return it == kv.end() ? std::string() : it->second;
}
}  // namespace

std::string Game::slotPath(int slot) const { return fileio::saveDir() + "/slot" + std::to_string(slot) + ".txt"; }

SlotInfo Game::readSlotInfo(int slot) const {
  SlotInfo si;
  std::string text;
  if (slot < 1 || slot > kSlots || !fileio::readFile(slotPath(slot), text)) return si;
  KV kv = parseKv(text);
  if (kv.find("version") == kv.end() || kv.find("seed") == kv.end()) return si;
  si.used = true;
  si.seed = (uint32_t)std::strtoul(strOf(kv, "seed").c_str(), nullptr, 10);
  si.playSecs = (int)numOf(kv, "playtime", 0);
  si.money = (int)numOf(kv, "money", 0);
  si.city = strOf(kv, "city");
  si.location = strOf(kv, "location");
  si.saved = (long long)std::atoll(strOf(kv, "saved").c_str());
  uint32_t bits = (uint32_t)numOf(kv, "progress", 0);
  int n = 0;
  for (int i = 0; i < kPgCount; ++i) n += (bits >> i) & 1u;
  si.progress = (float)n / (float)kPgCount;
  return si;
}

int Game::latestSlot() const {
  int best = 0;
  long long bt = -1;
  for (int i = 1; i <= kSlots; ++i) {
    SlotInfo si = readSlotInfo(i);
    if (si.used && si.saved >= bt) { bt = si.saved; best = i; }
  }
  return best;
}

void Game::refreshSlots() {
  for (int i = 0; i < kSlots; ++i) slots_[i] = readSlotInfo(i + 1);
}

bool Game::deleteSlot(int slot) {
  bool ok = fileio::removeFile(slotPath(slot));
  refreshSlots();
  return ok;
}

bool Game::saveGame() {
  if (phase_ != Phase::Playing) return false;
  return saveToSlot(activeSlot_);
}

bool Game::saveToSlot(int slot) {
  if (phase_ != Phase::Playing || slot < 1 || slot > kSlots) return false;
  std::ostringstream o;
  o.precision(7);
  o << "version=4\n";
  o << "seed=" << worldSeed_ << "\n";
  o << "saved=" << (long long)std::time(nullptr) << "\n";
  o << "city=" << world_.cityName << "\n";
  o << "location=" << locationName(player_.vehicle >= 0 ? vehicles_[player_.vehicle].pos : player_.pos, player_.indoors) << "\n";
  o << "progress=" << progress_ << "\nshops_visited=" << shopsVisited_ << "\ndriven=" << driven_ << "\n";
  o << "money=" << moneyCents_ << "\n";
  o << "playerX=" << player_.pos.x << "\nplayerZ=" << player_.pos.y << "\nplayerYaw=" << player_.yaw << "\n";
  o << "indoors=" << (player_.indoors ? 1 : 0) << "\n";
  o << "health=" << player_.health << "\nstamina=" << player_.stamina << "\nweapon=" << player_.weapon << "\n";
  // only the persistent cars (police units are temporary and respawn with the wanted level)
  o << "currentVehicle=" << (player_.vehicle >= 0 && !vehicles_[player_.vehicle].police ? player_.vehicle : -1) << "\n";
  for (const Vehicle& v : vehicles_) {
    if (v.police || v.despawn || v.traffic) continue;
    o << "veh" << v.id << "=" << v.pos.x << "," << v.pos.y << "," << v.yaw << "," << v.fuel << "," << v.health << "\n";
  }
  for (int w = 1; w < kWeaponCount; ++w)
    if (player_.owned[w]) o << "wpn_" << weaponDef(w).key << "=" << player_.mag[w] << "," << player_.reserve[w] << "\n";
  for (int i = 1; i < kItemCount; ++i) o << "item_" << itemDef(i).key << "=" << inventory_[i] << "\n";
  o << "camera=" << (cam_.mode() == CamMode::TopDown ? 0 : 1) << "\ncamZoom=" << cam_.topDownZoom() << "\n";
  o << "time_of_day=" << timeOfDay_ << "\nweather_target=" << weatherTarget_ << "\nplaytime=" << time_ << "\n";
  std::string s = o.str();
  bool ok = fileio::writeFileAtomic(slotPath(slot), s.data(), s.size());
  if (!ok) LOGW("save failed: %s", slotPath(slot).c_str());
  else activeSlot_ = slot;
  refreshSlots();
  return ok;
}

// Applies a parsed slot to the (already generated and reset) game.
void Game::applyStateFromFile(const KV& kv) {
  moneyCents_ = (int)numOf(kv, "money", 40000);
  moneyDisplay_ = (float)moneyCents_;
  player_.health = clamp(numOf(kv, "health", 100), 1.0f, 100.0f);
  player_.stamina = clamp(numOf(kv, "stamina", 100), 0.0f, 100.0f);
  for (int w = 1; w < kWeaponCount; ++w) {
    auto it = kv.find(std::string("wpn_") + weaponDef(w).key);
    if (it == kv.end()) continue;
    int mag = 0, res = 0;
    if (std::sscanf(it->second.c_str(), "%d,%d", &mag, &res) == 2) {
      player_.owned[w] = true;
      player_.mag[w] = clamp(mag, 0, weaponDef(w).magazine);
      player_.reserve[w] = clamp(res, 0, weaponDef(w).reserveMax);
    }
  }
  player_.weapon = clamp((int)numOf(kv, "weapon", 0), 0, kWeaponCount - 1);
  if (!player_.owned[player_.weapon]) player_.weapon = kWpnFists;
  for (Vehicle& v : vehicles_) {
    auto it = kv.find("veh" + std::to_string(v.id));
    if (it == kv.end()) continue;
    float x, z, yaw, fuel, hp;
    if (std::sscanf(it->second.c_str(), "%f,%f,%f,%f,%f", &x, &z, &yaw, &fuel, &hp) == 5) {
      v.pos = {x, z}; v.yaw = yaw;
      v.fuel = clamp(fuel, 0.0f, vehicleDef(v.model).fuelCap);
      v.health = clamp(hp, 0.0f, 100.0f);
      v.vel = {}; v.speed = 0;
    }
  }
  for (int i = 1; i < kItemCount; ++i) inventory_[i] = (int)numOf(kv, (std::string("item_") + itemDef(i).key).c_str(), (float)inventory_[i]);
  timeOfDay_ = numOf(kv, "time_of_day", 10.0f);
  weatherTarget_ = clamp(numOf(kv, "weather_target", 0.0f), 0.0f, 1.0f);
  rain_ = weatherTarget_;
  wetness_ = weatherTarget_ > 0.05f ? 1.0f : 0.0f;
  progress_ = (uint32_t)numOf(kv, "progress", 0);
  shopsVisited_ = (uint32_t)numOf(kv, "shops_visited", 0);
  driven_ = numOf(kv, "driven", 0);
  time_ = numOf(kv, "playtime", 0);
  cam_.init(numOf(kv, "camera", 0) > 0.5f ? CamMode::ThirdPerson : CamMode::TopDown);
  cam_.setTopDownZoom(numOf(kv, "camZoom", 30.0f));
  bool indoors = numOf(kv, "indoors", 0) > 0.5f;
  Vec2 pos{numOf(kv, "playerX", player_.pos.x), numOf(kv, "playerZ", player_.pos.y)};
  teleportPlayer(pos, numOf(kv, "playerYaw", player_.yaw));
  player_.indoors = indoors && world_.inInterior(pos.x, pos.y);
  int cv = (int)numOf(kv, "currentVehicle", -1);
  if (cv >= 0 && cv < (int)vehicles_.size()) {
    player_.vehicle = cv;
    vehicles_[cv].occupant = 0;
    vehicles_[cv].engineOn = true;
    player_.pos = vehicles_[cv].pos;
  }
  LOGI("Game loaded: slot %d seed %u money=%d vehicle=%d", activeSlot_, worldSeed_, moneyCents_, cv);
}

// Same-city reload (tests, quick revert): the slot's seed must match the generated world.
bool Game::loadGame() {
  std::string text;
  if (!fileio::readFile(slotPath(activeSlot_), text)) return false;
  KV kv = parseKv(text);
  if (kv.find("version") == kv.end()) return false;
  if ((uint32_t)std::strtoul(strOf(kv, "seed").c_str(), nullptr, 10) != worldSeed_) return false;
  applyStateFromFile(kv);
  return true;
}

void Game::onBackground() {
  if (phase_ == Phase::Playing) saveGame();
  input_.reset();
}

// ------------------------------------------------------------------------------------------------ settings
bool Game::saveSettings() const {
  std::ostringstream o;
  o.precision(5);
  const Settings& s = settings_;
  o << "quality=" << s.quality << "\ndynamicRes=" << s.dynamicRes << "\nshadows=" << s.shadows << "\ndrawDistance=" << s.drawDistance
    << "\nbloom=" << s.bloom << "\nweatherFx=" << s.weatherFx << "\nbrightness=" << s.brightness << "\nshowFps=" << s.showFps
    << "\nmaster=" << s.master << "\nsfx=" << s.sfx << "\nambience=" << s.ambience << "\nmuted=" << s.muted
    << "\nsensitivity=" << s.sensitivity << "\ninvertY=" << s.invertY << "\nisometric=" << s.isometric << "\nhudScale=" << s.hudScale << "\nhudOpacity=" << s.hudOpacity
    << "\naimAssist=" << s.aimAssist << "\nweatherMode=" << s.weatherMode << "\ndayCycle=" << s.dayCycle << "\nshowMinimap=" << s.showMinimap
    << "\nhints=" << s.hints << "\nautosave=" << s.autosave << "\ntextScale=" << s.textScale << "\nhighContrast=" << s.highContrast
    << "\nreduceMotion=" << s.reduceMotion << "\nreduceFlashes=" << s.reduceFlashes << "\n";
  std::string t = o.str();
  return fileio::writeFileAtomic(fileio::saveDir() + "/settings.txt", t.data(), t.size());
}

bool Game::loadSettings() {
  std::string text;
  if (!fileio::readFile(fileio::saveDir() + "/settings.txt", text)) return false;
  KV kv = parseKv(text);
  Settings& s = settings_;
  s.quality = clamp((int)numOf(kv, "quality", 2), 0, 3);
  s.dynamicRes = numOf(kv, "dynamicRes", 1) > 0.5f;
  s.shadows = numOf(kv, "shadows", 1) > 0.5f;
  s.drawDistance = clamp(numOf(kv, "drawDistance", 1.0f), 0.6f, 1.5f);
  s.bloom = numOf(kv, "bloom", 1) > 0.5f;
  s.weatherFx = numOf(kv, "weatherFx", 1) > 0.5f;
  s.brightness = clamp(numOf(kv, "brightness", 1.0f), 0.7f, 1.4f);
  s.showFps = numOf(kv, "showFps", 0) > 0.5f;
  s.master = clamp(numOf(kv, "master", 0.9f), 0.0f, 1.0f);
  s.sfx = clamp(numOf(kv, "sfx", 1.0f), 0.0f, 1.0f);
  s.ambience = clamp(numOf(kv, "ambience", 1.0f), 0.0f, 1.0f);
  s.muted = numOf(kv, "muted", 0) > 0.5f;
  s.sensitivity = clamp(numOf(kv, "sensitivity", 1.0f), 0.4f, 2.0f);
  s.invertY = numOf(kv, "invertY", 0) > 0.5f;
  s.isometric = numOf(kv, "isometric", 0) > 0.5f;
  s.hudScale = clamp(numOf(kv, "hudScale", 1.0f), 0.75f, 1.35f);
  s.hudOpacity = clamp(numOf(kv, "hudOpacity", 1.0f), 0.3f, 1.0f);
  s.aimAssist = numOf(kv, "aimAssist", 1) > 0.5f;
  s.weatherMode = clamp((int)numOf(kv, "weatherMode", 0), 0, 3);
  s.dayCycle = clamp((int)numOf(kv, "dayCycle", 1), 0, 2);
  s.showMinimap = numOf(kv, "showMinimap", 1) > 0.5f;
  s.hints = numOf(kv, "hints", 1) > 0.5f;
  s.autosave = numOf(kv, "autosave", 1) > 0.5f;
  s.textScale = clamp(numOf(kv, "textScale", 1.0f), 0.85f, 1.3f);
  s.highContrast = numOf(kv, "highContrast", 0) > 0.5f;
  s.reduceMotion = numOf(kv, "reduceMotion", 0) > 0.5f;
  s.reduceFlashes = numOf(kv, "reduceFlashes", 0) > 0.5f;
  return true;
}

}  // namespace gtabr
