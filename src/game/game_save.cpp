// Save / load: a small key=value text file written atomically in the app's private storage.
#include <cmath>
#include <cstdio>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <limits>

#include "../core/fileio.h"
#include "../core/save_store.h"
#include "../core/log.h"
#include "game.h"

namespace gtabr {

std::string Game::savePath() const { return fileio::saveDir() + "/slot_" + std::to_string(std::max(0, activeSlot_)) + ".sav"; }

bool Game::saveGame() {
  if (phase_ != Phase::Playing || !sessionActive_ || activeSlot_ < 0) return false;
  if(fadeThen_ || player_.entering || player_.exiting){toast("Aguarde a transição para salvar", "save");return false;}
  std::ostringstream o;
  o.precision(std::numeric_limits<float>::max_digits10);
  o << "version=5\nseed=" << worldSeed_ << "\ngenerator=1\n";
  o << "savedAt=" << std::time(nullptr) << "\nlocation=" << (player_.indoors ? "Interior" : world_.cityName) << "\n";
  o << "wanted=" << wanted_ << "\nheat=" << wantedHeat_ << "\npurchases=" << purchases_ << "\ntutorial=" << tutorialStep_ << "\n";
  o << "seen=" << sinceSeen_ << "\nlastKnownX=" << wantedLastKnown_.x << "\nlastKnownZ=" << wantedLastKnown_.y << "\n";

  o << "money=" << moneyCents_ << "\n";
  o << "playerX=" << player_.pos.x << "\nplayerZ=" << player_.pos.y << "\nplayerYaw=" << player_.yaw << "\n";
  o << "indoors=" << (player_.indoors ? 1 : 0) << "\n";
  o << "health=" << (player_.dead ? 100.0f : player_.health) << "\nstamina=" << player_.stamina << "\nweapon=" << player_.weapon << "\n";
  // only the persistent cars (police units are temporary and respawn with the wanted level)
  o << "currentVehicle=" << (player_.vehicle >= 0 && player_.vehicle < (int)vehicles_.size() && !vehicles_[player_.vehicle].police ? player_.vehicle : -1) << "\n";
  for (const Vehicle& v : vehicles_) {
    if (v.police || v.despawn) continue;
    o << "veh" << v.id << "=" << v.pos.x << "," << v.pos.y << "," << v.yaw << "," << v.fuel << "," << v.health << "\n";
  }
  for (int w = 1; w < kWeaponCount; ++w)
    if (player_.owned[w]) o << "wpn_" << weaponDef(w).key << "=" << player_.mag[w] << "," << player_.reserve[w] << "\n";
  for (int i = 1; i < kItemCount; ++i) o << "item_" << itemDef(i).key << "=" << inventory_[i] << "\n";
  o << "camera=" << (cam_.mode() == CamMode::TopDown ? 0 : 1) << "\ncamZoom=" << cam_.topDownZoom() << "\n";
  o << "set_sensitivity=" << settings_.sensitivity << "\nset_invertY=" << (settings_.invertY ? 1 : 0) << "\nset_shadows=" << (settings_.shadows ? 1 : 0)
    << "\nset_quality=" << settings_.quality << "\nset_dynres=" << (settings_.dynamicRes ? 1 : 0) << "\ntime_of_day=" << timeOfDay_ << "\nset_hudScale=" << settings_.hudScale << "\nset_showFps=" << (settings_.showFps ? 1 : 0) << "\n";
  o << "playtime=" << time_ << "\n";
  std::string s = o.str();
  bool ok = save_store::write(savePath(), s);
  if (ok) { std::string slot = std::to_string(activeSlot_); fileio::writeFileAtomic(fileio::saveDir() + "/recent.txt", slot.data(), slot.size()); writeSettings(); }
  if (!ok) LOGW("save failed: %s", savePath().c_str());
  return ok;
}

bool Game::loadGame() {
  std::string text;
  bool recovered=false;
  if (!save_store::read(savePath(), text,&recovered)) return false;
  std::istringstream in(text);
  std::string line;
  std::unordered_map<std::string, std::string> kv;
  while (std::getline(in, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    kv[line.substr(0, eq)] = line.substr(eq + 1);
  }
  if (kv.find("version") == kv.end()) return false;
  if (kv.count("seed") && (uint32_t)std::strtoull(kv["seed"].c_str(), nullptr, 10) != worldSeed_) return false;
  auto num = [&](const char* k, float def) {
    auto it = kv.find(k);
    if (it == kv.end()) return def;
    char* end = nullptr; float value = std::strtof(it->second.c_str(), &end);
    return end == it->second.c_str() || *end || !std::isfinite(value) ? def : value;
  };
  moneyCents_ = (int)clamp(num("money", 40000), 0.0f, 100000000.0f);
  wanted_ = clamp((int)num("wanted", 0), 0, 3); wantedHeat_ = clamp(num("heat", 0), 0.0f, 100.0f);
  sinceSeen_ = num("seen", 100000); wantedLastKnown_ = {num("lastKnownX", 0), num("lastKnownZ", 0)};
  purchases_ = clamp((int)num("purchases", 0), 0, 1000000); tutorialStep_ = clamp((int)num("tutorial", 0), 0, 10);
  moneyDisplay_ = (float)moneyCents_;
  player_.health = clamp(num("health", 100), 1.0f, 100.0f);
  player_.stamina = clamp(num("stamina", 100), 0.0f, 100.0f);
  for (int w=1;w<kWeaponCount;++w) {player_.owned[w]=false;player_.mag[w]=0;player_.reserve[w]=0;}
  for (auto& v:vehicles_) {v.occupant=-1;v.engineOn=false;}
  player_.vehicle=-1;
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
  player_.weapon = clamp((int)num("weapon", 0), 0, kWeaponCount - 1);
  if (!player_.owned[player_.weapon]) player_.weapon = kWpnFists;
  for (Vehicle& v : vehicles_) {
    auto it = kv.find("veh" + std::to_string(v.id));
    if (it == kv.end()) continue;
    float x, z, yaw, fuel, hp;
    if (std::sscanf(it->second.c_str(), "%f,%f,%f,%f,%f", &x, &z, &yaw, &fuel, &hp) == 5 && std::isfinite(x) && std::isfinite(z) && std::isfinite(yaw) && std::isfinite(fuel) && std::isfinite(hp) && world_.playArea.contains(x, z)) {
      v.pos = {x, z}; v.yaw = yaw;
      v.fuel = clamp(fuel, 0.0f, vehicleDef(v.model).fuelCap);
      v.health = clamp(hp, 0.0f, 100.0f);
      v.vel = {}; v.speed = 0;
    }
  }
  for (int i = 1; i < kItemCount; ++i) inventory_[i] = (int)clamp(num((std::string("item_") + itemDef(i).key).c_str(), (float)inventory_[i]), 0.0f, 10000.0f);
  settings_.sensitivity = num("set_sensitivity", 1.0f);
  settings_.invertY = num("set_invertY", 0) > 0.5f;
  settings_.shadows = num("set_shadows", 1) > 0.5f;
  settings_.quality = clamp((int)num("set_quality", 2), 0, 3);
  settings_.dynamicRes = num("set_dynres", 1) != 0;
  timeOfDay_ = std::fmod(std::max(0.0f,num("time_of_day",10.0f)),24.0f);
  settings_.hudScale = num("set_hudScale", 1.0f);
  settings_.showFps = num("set_showFps", 0) > 0.5f;
  time_ = std::max(0.0f,num("playtime", 0));
  cam_.init(num("camera", 0) > 0.5f ? CamMode::ThirdPerson : CamMode::TopDown);
  cam_.setTopDownZoom(num("camZoom", 30.0f));
  bool indoors = num("indoors", 0) > 0.5f;
  Vec2 pos{num("playerX", player_.pos.x), num("playerZ", player_.pos.y)};
  if (!world_.inInterior(pos.x, pos.y) && !world_.playArea.contains(pos.x, pos.y)) pos = {world_.spawnPlayer.x, world_.spawnPlayer.z};
  teleportPlayer(pos, num("playerYaw", player_.yaw));
  player_.swimming = world_.waterDepth(pos.x, pos.y) > 0.9f;
  player_.swimBlend=player_.swimming?1.0f:0.0f;
  if (player_.swimming) player_.y = world_.waterLevel - 0.85f;
  player_.indoors = indoors && world_.inInterior(pos.x, pos.y);
  int cv = (int)num("currentVehicle", -1);
  if (cv >= 0 && cv < (int)vehicles_.size()) {
    player_.vehicle = cv;
    vehicles_[cv].occupant = 0;
    vehicles_[cv].engineOn = true;
    player_.pos = vehicles_[cv].pos;
  }
  readSettings();
  if(recovered)toast("Save recuperado do backup", "save");
  LOGI("Game loaded: money=%d, vehicle=%d", moneyCents_, cv);
  return true;
}

void Game::onBackground() {
  if (phase_ == Phase::Playing && sessionActive_) {
    // Settle an in-flight interaction before Android suspends the process.
    if(fadeThen_){auto commit=std::move(fadeThen_);fadeThen_=nullptr;commit();fadeAlpha_=fadeTarget_=0;}
    if(player_.entering||player_.exiting)updatePlayer(0.75f,InputFrame{});
    if(player_.vehicle>=0&&player_.vehicle<(int)vehicles_.size()){
      const auto& vehicle=vehicles_[player_.vehicle];player_.pos=vehicle.pos;player_.yaw=player_.targetYaw=vehicle.yaw;
      player_.y=world_.heightAt(vehicle.pos.x,vehicle.pos.y);
    }
    saveGame();
  }
  writeSettings();
  input_.reset();
}

}  // namespace gtabr
