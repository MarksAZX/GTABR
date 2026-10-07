// Save / load: a small key=value text file written atomically in the app's private storage.
#include <cmath>
#include <cstdio>
#include <sstream>

#include "../core/fileio.h"
#include "../core/log.h"
#include "game.h"

namespace gtabr {

std::string Game::savePath() const { return fileio::saveDir() + "/save.txt"; }

bool Game::saveGame() {
  if (phase_ != Phase::Playing) return false;
  std::ostringstream o;
  o.precision(6);
  o << "version=3\n";
  o << "money=" << moneyCents_ << "\n";
  o << "playerX=" << player_.pos.x << "\nplayerZ=" << player_.pos.y << "\nplayerYaw=" << player_.yaw << "\n";
  o << "indoors=" << (player_.indoors ? 1 : 0) << "\n";
  o << "health=" << player_.health << "\nstamina=" << player_.stamina << "\nweapon=" << player_.weapon << "\n";
  o << "currentVehicle=" << player_.vehicle << "\n";
  for (const Vehicle& v : vehicles_) {
    o << "veh" << v.id << "=" << v.pos.x << "," << v.pos.y << "," << v.yaw << "," << v.fuel << "," << v.health << "\n";
  }
  for (int i = 1; i < kItemCount; ++i) o << "item_" << itemDef(i).key << "=" << inventory_[i] << "\n";
  o << "camera=" << (cam_.mode() == CamMode::TopDown ? 0 : 1) << "\ncamZoom=" << cam_.topDownZoom() << "\n";
  o << "set_sensitivity=" << settings_.sensitivity << "\nset_invertY=" << (settings_.invertY ? 1 : 0) << "\nset_shadows=" << (settings_.shadows ? 1 : 0)
    << "\nset_quality=" << settings_.quality << "\nset_dynres=" << (settings_.dynamicRes ? 1 : 0) << "\ntime_of_day=" << timeOfDay_ << "\nset_hudScale=" << settings_.hudScale << "\nset_showFps=" << (settings_.showFps ? 1 : 0) << "\n";
  o << "playtime=" << time_ << "\n";
  std::string s = o.str();
  bool ok = fileio::writeFileAtomic(savePath(), s.data(), s.size());
  if (!ok) LOGW("save failed: %s", savePath().c_str());
  return ok;
}

bool Game::loadGame() {
  std::string text;
  if (!fileio::readFile(savePath(), text)) return false;
  std::istringstream in(text);
  std::string line;
  std::unordered_map<std::string, std::string> kv;
  while (std::getline(in, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    kv[line.substr(0, eq)] = line.substr(eq + 1);
  }
  if (kv.find("version") == kv.end()) return false;
  auto num = [&](const char* k, float def) {
    auto it = kv.find(k);
    return it == kv.end() ? def : (float)std::atof(it->second.c_str());
  };
  moneyCents_ = (int)num("money", 40000);
  moneyDisplay_ = (float)moneyCents_;
  player_.health = clamp(num("health", 100), 1.0f, 100.0f);
  player_.stamina = clamp(num("stamina", 100), 0.0f, 100.0f);
  player_.weapon = (int)num("weapon", 0);
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
  for (int i = 1; i < kItemCount; ++i) inventory_[i] = (int)num((std::string("item_") + itemDef(i).key).c_str(), (float)inventory_[i]);
  settings_.sensitivity = num("set_sensitivity", 1.0f);
  settings_.invertY = num("set_invertY", 0) > 0.5f;
  settings_.shadows = num("set_shadows", 1) > 0.5f;
  settings_.quality = clamp((int)num("set_quality", 2), 0, 3);
  settings_.dynamicRes = num("set_dynres", 1) != 0;
  timeOfDay_ = (float)num("time_of_day", 10.0);
  settings_.hudScale = num("set_hudScale", 1.0f);
  settings_.showFps = num("set_showFps", 0) > 0.5f;
  time_ = num("playtime", 0);
  cam_.init(num("camera", 0) > 0.5f ? CamMode::ThirdPerson : CamMode::TopDown);
  cam_.setTopDownZoom(num("camZoom", 30.0f));
  bool indoors = num("indoors", 0) > 0.5f;
  Vec2 pos{num("playerX", player_.pos.x), num("playerZ", player_.pos.y)};
  teleportPlayer(pos, num("playerYaw", player_.yaw));
  player_.indoors = indoors && world_.inInterior(pos.x, pos.y);
  int cv = (int)num("currentVehicle", -1);
  if (cv >= 0 && cv < (int)vehicles_.size()) {
    player_.vehicle = cv;
    vehicles_[cv].occupant = 0;
    vehicles_[cv].engineOn = true;
    player_.pos = vehicles_[cv].pos;
  }
  LOGI("Game loaded: money=%d, vehicle=%d", moneyCents_, cv);
  return true;
}

void Game::onBackground() {
  if (phase_ == Phase::Playing) saveGame();
  input_.reset();
}

}  // namespace gtabr
