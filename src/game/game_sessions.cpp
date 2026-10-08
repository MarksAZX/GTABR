#include "game.h"
#include "../core/fileio.h"
#include "../core/save_store.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <unistd.h>

namespace gtabr {
namespace {
std::map<std::string,std::string> fields(const std::string& path) {
  std::string text; if(path.ends_with(".sav"))save_store::read(path,text);else fileio::readFile(path,text);
  std::istringstream in(text); std::string line; std::map<std::string,std::string> kv;
  while (std::getline(in,line)) { auto p=line.find('='); if(p!=std::string::npos) kv[line.substr(0,p)]=line.substr(p+1); }
  return kv;
}
float number(const std::map<std::string,std::string>& kv,const char* key,float def) {
  auto it=kv.find(key); if(it==kv.end()) return def;
  char* end=nullptr; float n=std::strtof(it->second.c_str(),&end);
  return end==it->second.c_str() || *end || !std::isfinite(n) ? def : n;
}
}
SaveSlot Game::inspectSlot(int slot) const {
  SaveSlot s; if(slot<0||slot>=4) return s;
  auto kv=fields(fileio::saveDir()+"/slot_"+std::to_string(slot)+".sav");
  std::string raw;auto path=fileio::saveDir()+"/slot_"+std::to_string(slot)+".sav";
  s.exists=access(path.c_str(),F_OK)==0||access((path+".bak").c_str(),F_OK)==0;
  s.valid=save_store::read(path,raw,&s.recovered);
  if(!s.valid)return s;
  s.seed=kv.count("seed") ? (uint32_t)std::strtoull(kv["seed"].c_str(),nullptr,10):1;
  s.money=(int)clamp(number(kv,"money",40000),0.0f,100000000.0f); s.playtime=std::max(0.0f,number(kv,"playtime",0));
  s.location=kv["location"]; if(s.location.empty()) s.location="Bairro";
  std::time_t stamp=(std::time_t)std::strtoll(kv["savedAt"].c_str(),nullptr,10);
  char buf[64]={}; if(auto tm=std::localtime(&stamp)) std::strftime(buf,sizeof(buf),"%d/%m/%Y %H:%M",tm);
  s.date=stamp ? buf : "Save anterior"; return s;
}
int Game::recentSlot() const {
  std::string last; fileio::readFile(fileio::saveDir()+"/recent.txt",last);
  int slot=last.empty()?-1:std::atoi(last.c_str());
  if(slot>=0&&slot<4&&inspectSlot(slot).valid) return slot;
  long long newest=-1; int result=-1;
  for(int i=0;i<4;++i) if(inspectSlot(i).valid) {
    auto kv=fields(fileio::saveDir()+"/slot_"+std::to_string(i)+".sav");
    long long t=std::strtoll(kv["savedAt"].c_str(),nullptr,10); if(t>newest){newest=t;result=i;}
  }
  return result;
}
void Game::readSeed() {
  auto kv=fields(savePath());
  worldSeed_=kv.count("seed") ? (uint32_t)std::strtoull(kv["seed"].c_str(),nullptr,10) : 1;
}
bool Game::startSlot(int slot,bool fresh,bool confirmed) {
  if(slot<0||slot>=4||phase_==Phase::Loading) return false;
  SaveSlot s=inspectSlot(slot);
  if(fresh&&s.exists&&!confirmed) return false;
  if(!fresh&&!s.valid) return false;
  if(sessionActive_ && activeSlot_!=slot && !saveGame()) { toast("Não foi possível salvar a partida atual", "save"); return false; }
  jobs_->waitIdle(); releaseWorld();
  activeSlot_=slot; freshSlot_=fresh; sessionActive_=true;
  if(fresh) {
    auto tick=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    worldSeed_=cfg_.seed ? cfg_.seed : (uint32_t)(tick^(tick>>32)); if(!worldSeed_) worldSeed_=1;
    timeOfDay_=10;
  } else readSeed();
  phase_=Phase::Loading; menu_=MenuState::None; worldReady_=false; loadingAnim_=0;
  closePanel(); input_.reset(); uiRects_.clear(); startWorldJob(); return true;
}
bool Game::deleteSlot(int slot) {
  if(slot<0||slot>=4||!inspectSlot(slot).exists) return false;
  if(sessionActive_&&slot==activeSlot_) return false;
  auto path=fileio::saveDir()+"/slot_"+std::to_string(slot)+".sav";
  bool ok=true;for(const char* ext:{"",".bak",".tmp",".bak.tmp"}){auto file=path+ext;if(access(file.c_str(),F_OK)==0&&std::remove(file.c_str())!=0)ok=false;}
  return ok;
}
void Game::handleBack() {
  if(phase_==Phase::Loading)return;
  if(menu_==MenuState::None)openPauseMenu();
  else if(menu_==MenuState::Main)quit_=true;
  else if(menu_==MenuState::Confirm)menu_=MenuState::Slots;
  else if(menu_==MenuState::Controls){writeSettings();menu_=MenuState::Settings;controlDrag_=-1;}
  else if(menu_==MenuState::Pause)menu_=MenuState::None;
  else menuAction(304);
}
void Game::returnToMain() {
  if(sessionActive_&&!saveGame()) {toast("Falha ao salvar. A partida continua aberta.","save");return;}
  closePanel(); wheel_.open=false; wheel_.anim=0; fueling_.active=false;
  sessionActive_=false; menu_=MenuState::Main; uiRects_.clear(); input_.reset();
  audio_.loopStop(sirenHandle_); sirenHandle_=0; audio_.loopStop(surfHandle_);surfHandle_=0;
}
void Game::readSettings() {
  // One-time migration preserves the original file as a backup.
  std::string marker;
  if(!fileio::readFile(fileio::saveDir()+"/legacy_migrated",marker)) {
    std::string legacy; bool ok=true;
    if(!inspectSlot(0).exists && fileio::readFile(fileio::saveDir()+"/save.txt",legacy)) ok=fileio::writeFileAtomic(fileio::saveDir()+"/slot_0.sav",legacy.data(),legacy.size());
    if(ok) fileio::writeFileAtomic(fileio::saveDir()+"/legacy_migrated","1",1);
  }
  auto kv=fields(fileio::saveDir()+"/settings.txt"); if(kv.empty()) return;
  settings_.quality=clamp((int)number(kv,"quality",2),0,3);
  settings_.sensitivity=clamp(number(kv,"sensitivity",1),0.4f,2.0f);
  settings_.hudScale=clamp(number(kv,"hudScale",1),0.75f,1.35f);
  settings_.volume=clamp(number(kv,"volume",0.9f),0.0f,1.0f);
  settings_.resolution=clamp(number(kv,"resolution",1),0.6f,1.0f);
  settings_.renderDistance=clamp(number(kv,"distance",1),0.6f,1.4f);
  settings_.vegetation=clamp(number(kv,"vegetation",1),0.25f,1.0f);
  settings_.shadows=number(kv,"shadows",1)!=0; settings_.dynamicRes=number(kv,"dynamic",1)!=0;
  settings_.invertY=number(kv,"invert",0)!=0; settings_.showFps=number(kv,"fps",0)!=0;
  settings_.fpsLimit=number(kv,"fpsLimit",30)>=60?60:30;
  settings_.effects=number(kv,"effects",1)!=0; settings_.dayCycle=number(kv,"dayCycle",1)!=0;
  settings_.controlScale=clamp(number(kv,"controlScale",1),0.7f,1.4f);
  settings_.controlOpacity=clamp(number(kv,"controlOpacity",0.8f),0.3f,1.0f);
  settings_.fixedJoystick=number(kv,"fixedJoystick",0)!=0;settings_.leftHanded=number(kv,"leftHanded",0)!=0;
  settings_.subtitles=number(kv,"subtitles",1)!=0;settings_.soundCaptions=number(kv,"soundCaptions",0)!=0;
  settings_.highContrast=number(kv,"highContrast",0)!=0;settings_.reducedMotion=number(kv,"reducedMotion",0)!=0;
  settings_.reflections=clamp((int)number(kv,"reflections",1),0,2);
  for(int i=0;i<9;++i){std::string x="ctrl"+std::to_string(i)+"X",y="ctrl"+std::to_string(i)+"Y";
    settings_.controlPos[i]={clamp(number(kv,x.c_str(),-1),-1.0f,0.95f),clamp(number(kv,y.c_str(),-1),-1.0f,0.94f)};}

}
bool Game::writeSettings() {
  std::ostringstream s; s<<"quality="<<settings_.quality<<"\nsensitivity="<<settings_.sensitivity<<"\nhudScale="<<settings_.hudScale
    <<"\nvolume="<<settings_.volume<<"\nresolution="<<settings_.resolution<<"\ndistance="<<settings_.renderDistance<<"\nvegetation="<<settings_.vegetation
    <<"\nshadows="<<settings_.shadows<<"\ndynamic="<<settings_.dynamicRes<<"\ninvert="<<settings_.invertY<<"\nfps="<<settings_.showFps
    <<"\nfpsLimit="<<settings_.fpsLimit<<"\neffects="<<settings_.effects<<"\ndayCycle="<<settings_.dayCycle<<"\n";
  s<<"controlScale="<<settings_.controlScale<<"\ncontrolOpacity="<<settings_.controlOpacity<<"\nfixedJoystick="<<settings_.fixedJoystick
   <<"\nleftHanded="<<settings_.leftHanded<<"\nsubtitles="<<settings_.subtitles<<"\nsoundCaptions="<<settings_.soundCaptions
   <<"\nhighContrast="<<settings_.highContrast<<"\nreducedMotion="<<settings_.reducedMotion<<"\nreflections="<<settings_.reflections<<"\n";
  for(int i=0;i<9;++i)s<<"ctrl"<<i<<"X="<<settings_.controlPos[i].x<<"\nctrl"<<i<<"Y="<<settings_.controlPos[i].y<<"\n";
  auto text=s.str();return fileio::writeFileAtomic(fileio::saveDir()+"/settings.txt",text.data(),text.size());
}
void Game::executeCode(int code) {
  switch(code) {
    case 0: player_.health=100; player_.stamina=100; player_.dead=false; player_.down=false; deathT_=0; break;
    case 1: for(int i=1;i<kWeaponCount;++i) giveWeapon(i,weaponDef(i).reserveMax); break;
    case 2: for(int i=1;i<kWeaponCount;++i) if(player_.owned[i]) {player_.mag[i]=weaponDef(i).magazine;player_.reserve[i]=weaponDef(i).reserveMax;} break;
    case 3: moneyCents_=std::min(100000000,moneyCents_+100000);moneyShow_=4;break;
    case 4: wanted_=0;wantedHeat_=0;sinceSeen_=1e9f;evadeT_=0;for(auto& n:npcs_)if(n.police)n.despawn=true;for(auto& v:vehicles_)if(v.police)v.despawn=true;audio_.loopStop(sirenHandle_);sirenHandle_=0;break;
    case 5: wanted_=std::min(3,wanted_+1);wantedHeat_=wanted_==1?1:wanted_==2?4:7;wantedLastKnown_=player_.pos;sinceSeen_=0;policeSpawnT_=0;break;
    case 6: case 7: {
      int vi=player_.vehicle>=0?player_.vehicle:nearestVehicleTo(player_.pos,8);
      if(vi<0){toast("Aproxime-se de um veículo", "car");return;}
      if(code==6){vehicles_[vi].health=100;vehicles_[vi].wrecked=false;vehicles_[vi].engineOn=player_.vehicle==vi;}
      else vehicles_[vi].fuel=vehicleDef(vehicles_[vi].model).fuelCap;
      break;
    }
    default:return;
  }
  toast("Código ativado", "check");
}
} // namespace
