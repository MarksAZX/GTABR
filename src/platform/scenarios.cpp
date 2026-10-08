// Scripted scenarios for the headless runner. "mvp" plays the whole target loop with real (injected) touch input:
// walk -> switch camera -> enter car -> drive -> refuel at the pump -> market (talk + buy) -> workshop repair -> save -> reload.
#include <cmath>
#include <string>
#include <sstream>
#include <map>
#include "../core/fileio.h"
#include "../core/save_store.h"

#include "../core/log.h"
#include "../core/png.h"
#include "../game/game.h"

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
        if (d.length() > 0.5f) { Vec2 n = d.normalized(); p += Vec2{-n.y, n.x} * 1.5f; }
        if ((p - g.vehicles()[g.player().vehicle].pos).length() < 5.0f) { prev = route[i]; continue; }
        if (!driveTo(p, 4.5f, maxFrames, maxSpeed)) return false;
      } else if (!driveTo(p, tol, maxFrames, std::min(maxSpeed, 8.0f))) return false;
      prev = route[i];
    }
    return true;
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
  if(name=="visual"){
    b.render=false;g.settings().dynamicRes=false;g.settings().dayCycle=false;g.settings().weatherMode=1;
    g.setTimeOfDay(16,0);b.idle(60);b.render=true;b.idle(35);b.shot("01_urban_day");if(g.camera().mode()==CamMode::TopDown)g.toggleCamera();b.idle(35);b.shot("02_urban_street");
    if(g.settings().quality>0){auto reference=fd;std::vector<uint8_t> off,on;uint32_t width=0,height=0;reference.globals.effectsInfo.x=0;r.requestReadback();r.renderFrame(reference);CHECK(r.readback(off,width,height),"AO disabled readback");reference.globals.effectsInfo.x=0.55f;r.requestReadback();r.renderFrame(reference);CHECK(r.readback(on,width,height),"AO enabled readback");int changed=0;for(size_t i=0;i<off.size()&&i<on.size();i+=4)if(std::abs(int(off[i])-int(on[i]))+std::abs(int(off[i+1])-int(on[i+1]))+std::abs(int(off[i+2])-int(on[i+2]))>3)++changed;CHECK(changed>5,"ambient occlusion affects real geometry pixels");LOGI("AO frozen-frame changed pixels: %d",changed);}

    CHECK(g.world().npcs.size()>45,"city has a larger deterministic population");
    CHECK(g.vehicles().size()>3,"seed creates real ambient vehicles");
    int moving=0;std::vector<Vec2> positions;for(auto& v:g.vehicles())positions.push_back(v.pos);
    b.render=false;b.idle(300);for(size_t i=3;i<positions.size();++i)if((g.vehicles()[i].pos-positions[i]).length()>2)++moving;CHECK(moving>0,"ambient vehicles travel using existing physics");
    g.settings().weatherMode=2;b.idle(1500);CHECK(g.weather().rain>0.9f&&g.weather().wet>0.8f,"rain really wets the ground");
    b.render=true;b.idle(8);b.shot("03_urban_rain");g.setTimeOfDay(21,0);b.idle(8);b.shot("04_urban_night_rain");
    float clock=g.weather().clock,wet=g.weather().wet;CHECK(g.saveGame(),"weather session saves");b.render=false;b.idle(180);CHECK(g.loadGame(),"weather session reloads");CHECK(std::fabs(g.weather().clock-clock)<0.01f&&std::fabs(g.weather().wet-wet)<0.001f,"rain timeline and wetness restore exactly");
    if(g.world().coastSide>=0){const auto& w=g.world();g.settings().weatherMode=1;b.idle(400);g.setTimeOfDay(16,0);g.teleportPlayer({w.poiBeach.x,w.poiBeach.z},w.coastSide*kPi*0.5f);b.idle(45);b.render=true;b.idle(45);b.shot("05_beach");
      Vec2 water{w.poiBeach.x,w.poiBeach.z};if(w.coastSide==0)water.y=-w.shoreline-3;if(w.coastSide==1)water.x=w.shoreline+3;if(w.coastSide==2)water.y=w.shoreline+3;if(w.coastSide==3)water.x=-w.shoreline-3;
      g.teleportPlayer(water,w.coastSide*kPi*0.5f);b.idle(12);b.shot("06_shallow_water");bool ring=false;for(auto& d:fd.decals)if(d.kind==2)ring=true;CHECK(ring,"water contact creates real ripple decals");
      if(w.coastSide==0)water.y-=12;if(w.coastSide==1)water.x+=12;if(w.coastSide==2)water.y+=12;if(w.coastSide==3)water.x-=12;g.teleportPlayer(water,w.coastSide*kPi*0.5f);b.idle(18);b.shot("07_swimming");CHECK(g.player().swimming,"deep water preserves swimming");
    }
    if(g.vehicles().size()>3){b.render=false;int id=3;auto& v=g.vehicles()[id];v.vel={};v.speed=0;g.player().vehicle=-1;g.teleportPlayer(v.pos,0);InputFrame enter;enter.enterExitPressed=true;b.step(enter);b.idle(28);CHECK(g.player().vehicle==id&&!g.vehicles()[id].ambientTraffic,"ambient vehicle can be taken over through real interaction");CHECK(g.saveGame(),"taken vehicle saves");g.vehicles()[id].pos={0,0};CHECK(g.loadGame()&&g.player().vehicle==id&&!g.vehicles()[id].ambientTraffic,"taken vehicle restores without AI control");InputFrame leave;leave.enterExitPressed=true;b.step(leave);b.idle(28);}
    g.player().vehicle=-1;g.teleportPlayer({g.world().spawnPlayer.x,g.world().spawnPlayer.z},0);b.render=false;b.idle(10);CHECK(g.saveGame(),"visual scenario leaves a valid slot");LOGI("VISUAL checks failures: %d",g_failures);return g_failures?1:0;
  }
  if (name == "resume") {
    std::string text;
    CHECK(save_store::read(fileio::saveDir()+"/slot_"+std::to_string(g.activeSlot())+".sav",text),"fresh process finds persisted slot");
    std::map<std::string,std::string> kv;std::istringstream in(text);std::string line;
    while(std::getline(in,line)){auto p=line.find('=');if(p!=std::string::npos)kv[line.substr(0,p)]=line.substr(p+1);}
    CHECK(g.world().seed==(uint32_t)std::strtoull(kv["seed"].c_str(),nullptr,10),"fresh process reconstructs saved seed instead of command-line seed");
    CHECK(g.money()==std::atoi(kv["money"].c_str()),"fresh process restores money");
    Vec2 expected{std::strtof(kv["playerX"].c_str(),nullptr),std::strtof(kv["playerZ"].c_str(),nullptr)};
    CHECK((g.player().pos-expected).length()<0.1f,"fresh process restores position");
    for(int i=1;i<kItemCount;++i)CHECK(g.itemCount(i)==std::atoi(kv[std::string("item_")+itemDef(i).key].c_str()),"fresh process restores inventory");
    for(int w=1;w<kWeaponCount;++w){auto it=kv.find(std::string("wpn_")+weaponDef(w).key);if(it!=kv.end()){int mag=0,res=0;std::sscanf(it->second.c_str(),"%d,%d",&mag,&res);CHECK(g.player().owned[w]&&g.player().mag[w]==mag&&g.player().reserve[w]==res,"fresh process restores weapon and ammunition");}}
    b.idle(4);b.shot("resume");LOGI("RESUME checks failures: %d",g_failures);return g_failures?1:0;
  }
  if (name == "city" || name == "menus" || name == "polish") {
    auto load=[&](){for(int i=0;i<1500&&!g.loaded();++i){g.frame(dt,fd);r.renderFrame(fd);}CHECK(g.loaded(),"city generation finishes");};
    auto tap=[&](float x,float y){InputFrame in;UiPointer p;p.id=0;p.pos={x,y};p.pressed=true;p.down=true;in.ui={p};b.step(in);p.pressed=false;p.down=false;p.released=true;in.ui={p};b.step(in);b.idle(2);};
    if(name=="menus"||name=="polish"){
      CHECK(g.menu()==MenuState::Main,"startup shows main menu");b.shot("00_main_menu");
      Vec4 bounds;CHECK(g.uiButtonBounds(401,bounds),"new game button is visible");
      tap(bounds.x+bounds.z/2,bounds.y+bounds.w/2);CHECK(g.menu()==MenuState::Slots,"new game button opens slots");b.shot("01_slots");
      CHECK(g.uiButtonBounds(420,bounds),"create button is visible");
      tap(bounds.x+bounds.z/2,bounds.y+bounds.w/2);load();CHECK(g.menu()==MenuState::None,"create slot starts gameplay");
    }
    if(name=="polish"){
      auto tapButton=[&](int id){
        b.idle(8);Vec4 rect;
        for(int page=0;page<8&&!g.uiButtonBounds(id,rect);++page){Vec4 next;if(!g.uiButtonBounds(791,next))break;tap(next.x+next.z/2,next.y+next.w/2);}
        if(!g.uiButtonBounds(id,rect)){CHECK(false,"requested UI control is reachable");return false;}
        CHECK(rect.x>=0&&rect.y>=0&&rect.x+rect.z<=r.outputWidth()+1&&rect.y+rect.w<=r.outputHeight()+1,"menu button fits viewport");
        tap(rect.x+rect.z/2,rect.y+rect.w/2);return true;
      };
      g.showMenu(MenuState::Pause);b.idle(8);tapButton(202);tapButton(700);
      int oldReflection=g.settings().reflections;tapButton(719);
      CHECK(g.settings().reflections==(oldReflection+1)%3,"reflection UI changes real setting");
      CHECK(fd.globals.reflectionInfo.y>0,"coastal reflection activates ray-march budget");
      tapButton(719);CHECK(fd.globals.reflectionInfo.x==0&&fd.globals.reflectionInfo.y==0,"reflection off removes reflection work");
      tapButton(719);CHECK(fd.globals.reflectionInfo.x==1&&fd.globals.reflectionInfo.y==0,"sky reflection avoids coastal ray-marching");
      tapButton(303);CHECK(g.settings().quality==3&&fd.globals.reflectionInfo.y==28,"ultra preset enables its coastal reflection budget");
      tapButton(702);float scale=g.settings().controlScale;tapButton(720);CHECK(g.settings().controlScale>scale,"control size increases");
      tapButton(727);CHECK(g.settings().leftHanded,"left handed layout enabled");
      tapButton(728);CHECK(g.menu()==MenuState::Controls,"control editor opens");
      b.idle(8);Vec4 joy;CHECK(g.uiButtonBounds(808,joy),"joystick has draggable control");
      InputFrame drag;UiPointer finger;finger.id=3;finger.down=true;finger.pressed=true;finger.pos={joy.x+joy.z/2,joy.y+joy.w/2};drag.ui={finger};b.step(drag);
      finger.pressed=false;finger.pos={r.outputWidth()*0.7f,r.outputHeight()*0.3f};drag.ui={finger};b.step(drag);
      finger.down=false;finger.released=true;drag.ui={finger};b.step(drag);b.idle(3);
      CHECK(g.settings().fixedJoystick&&std::fabs(g.settings().controlPos[8].x-0.7f)<0.01f,"drag changes and persists joystick position");
      b.shot("controls_custom");tapButton(304);tapButton(704);tapButton(722);tapButton(725);
      CHECK(g.settings().highContrast&&g.settings().reducedMotion,"accessibility options toggle");
      tapButton(717);b.shot("accessibility_settings");tapButton(304);b.shot("pause_polished");
      tapButton(205);b.shot("inventory_polished");tapButton(304);tapButton(200);
      auto layout=g.controlLayout();layout.modal=false;InputSystem input;input.poll(layout);
      input.onTouch(5,TouchAction::Down,layout.joyCenter.x,layout.joyCenter.y);input.poll(layout);
      input.onTouch(5,TouchAction::Move,layout.joyCenter.x+layout.joyRadius*0.7f,layout.joyCenter.y);
      auto moved=input.poll(layout);CHECK(moved.joyActive&&moved.move.x>0.5f,"relocated joystick receives movement at its visual position");
      g.showMenu(MenuState::Settings);tapButton(702);tapButton(728);tapButton(810);tapButton(304);tapButton(304);g.showMenu(MenuState::None);
    }
    b.render=false;b.idle(45);
    CHECK(g.world().shops.size()==4,"four enterable stores");
    uint32_t seed=g.world().seed;auto spawn=g.world().spawnPlayer;
    for(int shop=0;shop<4;++shop){
      auto sh=g.world().shops[shop];int door=-1;
      for(const auto& d:g.world().doors)if(d.shop==shop&&d.toInterior)door=d.id;
      CHECK(door>=0,"shop has entrance");
      g.teleportPlayer({sh.door.x,sh.door.z},0);b.idle(2);
      Interactable it;it.kind=IKind::Door;it.id=door;g.activateInteractable(it);
      if(name=="polish"&&shop==0){g.onBackground();CHECK(g.player().indoors,"background during doorway settles inside before saving");}
      b.idle(20);
      CHECK(g.player().indoors&&g.world().interiorAt(g.player().pos.x,g.player().pos.y)==sh.interior,"door enters correct interior");
      int clerk=-1;for(size_t n=0;n<g.npcs().size();++n)if(g.npcs()[n].role==4&&g.world().interiorAt(g.npcs()[n].pos.x,g.npcs()[n].pos.y)==sh.interior)clerk=(int)n;
      CHECK(clerk>=0,"clerk exists in each store");it.kind=IKind::Npc;it.id=clerk;g.activateInteractable(it);g.selectPanelOptionPublic(0);
      CHECK(g.panel().open&&g.panel().title==sh.name,"clerk opens shop-specific catalog");
      auto stock=sh.stock[0];int money=g.money(),count=g.itemCount(stock.id);g.selectPanelOptionPublic(0);
      CHECK(g.money()==money-stock.priceCents,"purchase deducts actual shop price");
      if(stock.kind==0)CHECK(g.itemCount(stock.id)==count+1,"purchase adds item to inventory");else CHECK(g.player().owned[stock.id],"hardware store gives real equipment");
      b.render=true;b.idle(20);b.shot("shop_"+std::to_string(shop));b.render=false;
      g.selectPanelOptionPublic((int)g.panel().options.size()-1);
      if(name=="polish"){if(g.camera().mode()==CamMode::TopDown)g.toggleCamera();b.render=true;b.idle(30);b.shot("interior_"+std::to_string(shop));b.render=false;}
      g.money()=0;CHECK(!g.buyStock(shop,0),"insufficient funds reject purchase");g.money()=40000;
    }
    g.teleportPlayer({spawn.x,spawn.z},0);b.idle(2);
    if(name=="polish"){
      auto& car=g.vehicles()[0];g.teleportPlayer(car.pos-right2(car.yaw)*(vehicleDef(car.model).width*0.5f+0.8f),car.yaw);
      b.press(&InputFrame::enterExitPressed);CHECK(g.player().entering&&g.player().vehicle==-1,"car entry has a visible transition before occupancy");
      CHECK(!g.saveGame(),"manual save waits for a stable interaction state");
      g.onBackground();CHECK(g.player().vehicle==0&&!g.player().entering,"background completes car entry before persistence");
      CHECK((g.player().pos-g.vehicles()[0].pos).length()<0.01f,"background saves canonical vehicle position");
      b.press(&InputFrame::enterExitPressed);CHECK(g.player().exiting,"car exit has a transition");g.onBackground();
      CHECK(!g.player().exiting&&g.player().vehicle==-1,"background completes car exit before persistence");
      g.teleportPlayer({spawn.x,spawn.z},0);b.idle(2);
    }
    g.player().health=12;g.executeCode(0);CHECK(g.player().health==100,"health code changes state");
    g.executeCode(1);CHECK(g.player().owned[kWpnPistol],"weapons code grants weapons");
    g.player().mag[kWpnPistol]=0;g.executeCode(2);CHECK(g.player().mag[kWpnPistol]>0,"ammo code replenishes magazines");
    int before=g.money();g.executeCode(3);CHECK(g.money()==before+100000,"money code grants money");
    g.executeCode(5);CHECK(g.wantedLevel()>0,"wanted code raises wanted level");g.executeCode(4);CHECK(g.wantedLevel()==0,"clear code removes wanted level");
    g.player().vehicle=0;g.vehicles()[0].health=1;g.vehicles()[0].fuel=0;g.executeCode(6);g.executeCode(7);
    CHECK(g.vehicles()[0].health==100&&g.vehicles()[0].fuel==vehicleDef(0).fuelCap,"vehicle codes repair and fill tank");g.player().vehicle=-1;
    if(g.world().coastSide>=0){auto& w=g.world();Vec2 p{w.poiBeach.x,w.poiBeach.z};int side=w.coastSide;
      g.teleportPlayer(p,0);b.idle(60);CHECK(!g.player().swimming,"dry beach uses walking state");
      if(g.camera().mode()==CamMode::TopDown)g.toggleCamera();
      CameraInput coastCamera;coastCamera.focus={p.x,g.player().y,p.y};coastCamera.headingYaw=side*kPi*0.5f;
      g.camera().snapTo(coastCamera,w,(float)r.outputWidth()/std::max(1u,r.outputHeight()));
      b.render=true;b.idle(45);b.shot("beach_shore");b.render=false;
      if(side==0)p.y=-w.shoreline-20;if(side==1)p.x=w.shoreline+20;if(side==2)p.y=w.shoreline+20;if(side==3)p.x=-w.shoreline-20;
      if(name=="polish"){
        Vec2 outward=(side==0?Vec2{0,-1}:side==1?Vec2{1,0}:side==2?Vec2{0,1}:Vec2{-1,0});
        Vec2 edge=p-outward*10.0f;g.teleportPlayer(edge,0);b.idle(3);CHECK(!g.player().swimming,"shallow water still permits walking");
        bool blended=false;
        for(int frame=0;frame<50;++frame){float yaw=g.camera().yaw();Vec2 forward{std::sin(yaw),-std::cos(yaw)},right{std::cos(yaw),std::sin(yaw)};
          InputFrame swim;swim.move={outward.dot(right),outward.dot(forward)};b.step(swim);
          if(g.player().swimming){blended=g.player().swimBlend>0&&g.player().swimBlend<0.99f;break;}}
        CHECK(blended,"walking into deeper water blends movement and buoyancy gradually");
      }
      g.teleportPlayer(p,0);b.idle(10);CHECK(g.player().swimming,"deep water switches to swimming");
      CHECK(g.player().y>w.heightAt(p.x,p.y)+0.4f,"swimmer floats above seabed");
      b.render=true;b.idle(20);b.shot("beach_swimming");b.render=false;
      g.teleportPlayer({w.poiBeach.x,w.poiBeach.z},0);b.idle(10);CHECK(!g.player().swimming,"returning to shore restores walking");
      g.teleportPlayer(p,0);b.idle(10);
    }
    g.money()=123456;g.player().health=67;g.player().mag[kWpnPistol]=7;g.equipWeapon(kWpnPistol);
    Vec2 saved=g.player().pos;int waterCount=g.itemCount(1);CHECK(g.saveGame(),"slot is saved atomically");
    CHECK(g.inspectSlot(g.activeSlot()).seed==seed,"save contains exact seed");
    int slot=g.activeSlot();g.returnToMain();CHECK(g.menu()==MenuState::Main,"return to menu saves and freezes session");
    CHECK(!g.startSlot(slot,true),"existing slot cannot be overwritten without confirmation");
    CHECK(g.startSlot(slot,false),"load starts city reconstruction");load();b.idle(2);
    CHECK(g.world().seed==seed&&g.world().spawnPlayer.x==spawn.x,"same seed regenerates same city");
    CHECK((g.player().pos-saved).length()<0.1f,"position restored");CHECK(g.money()==123456&&g.itemCount(1)==waterCount,"money and inventory restored");
    CHECK(g.player().owned[kWpnPistol]&&g.player().mag[kWpnPistol]==7,"weapons and ammunition restored");
    if(name=="polish"){
      CHECK(g.settings().highContrast&&g.settings().reducedMotion,"saved accessibility survives slot reload");
      const auto& w=g.world();if(w.coastSide>=0){
        Vec2 water{w.poiBeach.x,w.poiBeach.z};
        if(w.coastSide==0)water.y=-w.shoreline-1;if(w.coastSide==1)water.x=w.shoreline+1;if(w.coastSide==2)water.y=w.shoreline+1;if(w.coastSide==3)water.x=-w.shoreline-1;
        g.teleportPlayer(water,0);b.idle(8);b.render=true;
        Vec2 along=(w.coastSide%2)?Vec2{0,1}:Vec2{1,0};
        // A parked car above the water line supplies visible geometry for coastal SSR.
        auto original=g.vehicles()[0];auto& car=g.vehicles()[0];Vec2 outward=(w.coastSide==0?Vec2{0,-1}:w.coastSide==1?Vec2{1,0}:w.coastSide==2?Vec2{0,1}:Vec2{-1,0});car.pos=water+along*2.5f+outward*4;car.driver=-1;car.engineOn=false;car.vel={};car.speed=0;
        CameraInput ci;ci.focus={water.x,0,water.y};ci.headingYaw=w.coastSide*kPi*0.5f+0.5f;g.camera().snapTo(ci,w,(float)r.outputWidth()/r.outputHeight());
        for(int mode=0;mode<3;++mode){g.settings().reflections=mode;b.idle(5);b.shot("coast_reflection_"+std::to_string(mode));}
        b.idle(4);auto reference=fd;reference.globals.reflectionInfo.x=1;reference.globals.reflectionInfo.y=0;
        std::vector<uint8_t> sky,ssr;uint32_t width=0,height=0;
        r.requestReadback();r.renderFrame(reference);CHECK(r.readback(sky,width,height),"sky-only reflection readback succeeds");
        reference.globals.reflectionInfo.y=28;r.requestReadback();r.renderFrame(reference);CHECK(r.readback(ssr,width,height),"coastal SSR readback succeeds");
        int changed=0;for(size_t pixel=0;pixel+3<sky.size()&&pixel+3<ssr.size();pixel+=4){int difference=0;for(int c=0;c<3;++c)difference+=std::abs(int(sky[pixel+c])-int(ssr[pixel+c]));if(difference>3)++changed;}
        LOGI("coastal SSR changes %d visible pixels with frozen scene/time",changed);CHECK(changed>5,"coastal SSR visibly reflects scene geometry beyond the sky fallback");
        if(!ssr.empty())writePng(out+"/coast_ssr_verified.png",ssr.data(),width,height);
        g.vehicles()[0]=original;g.settings().reflections=2;b.render=false;
      }
      g.teleportPlayer({spawn.x,spawn.z},0);b.idle(3);g.saveGame();
      g.returnToMain();auto info=g.inspectSlot(slot);
      const std::string path=fileio::saveDir()+"/slot_"+std::to_string(slot)+".sav";
      CHECK(fileio::writeFileAtomic(path,"truncated",9),"inject truncated primary save");
      auto backup=g.inspectSlot(slot);CHECK(backup.valid&&backup.recovered&&backup.seed==seed,"slot screen finds valid backup and its seed");
      CHECK(g.startSlot(slot,false),"corrupted primary loads its backup");load();b.idle(2);
      CHECK(g.world().seed==seed&&g.money()==backup.money,"recovered game rebuilds correct city and money");
      CHECK(g.saveGame(),"recovered slot can be saved again");
    }
    g.showMenu(MenuState::Map);b.render=true;b.idle(4);b.shot("full_map");g.showMenu(MenuState::Codes);b.shot("codes");g.showMenu(MenuState::Settings);b.shot("settings");g.showMenu(MenuState::Pause);b.shot("pause");
    g.returnToMain();CHECK(g.startSlot(1,true,true),"second slot starts independently");load();
    CHECK(g.inspectSlot(slot).money==123456,"new slot preserves original save");
    g.returnToMain();CHECK(g.deleteSlot(1),"inactive slot can be deleted");CHECK(!g.inspectSlot(1).exists,"deleted slot disappears");
    CHECK(g.recentSlot()==slot,"continue falls back to most recent remaining slot");
    LOGI("CITY checks failures: %d",g_failures);return g_failures?1:0;
  }

  if (name == "diagnostic") {
    b.idle(40);
    for(int i=0;i<4;++i){g.frame(dt,fd);
      if(i==0){fd.worldMeshes.clear();fd.models.clear();fd.sprites.clear();fd.spriteBatches.clear();fd.decals.clear();fd.silhouettes.clear();fd.silhouetteBatches.clear();fd.ui.clear();fd.uiBatches.clear();}
      if(i==1){fd.sprites.clear();fd.spriteBatches.clear();fd.decals.clear();fd.silhouettes.clear();fd.silhouetteBatches.clear();fd.ui.clear();fd.uiBatches.clear();}
      if(i==2){fd.ui.clear();fd.uiBatches.clear();}
      r.requestReadback();r.renderFrame(fd);std::vector<uint8_t> px;uint32_t w,h;if(r.readback(px,w,h))writePng(out+"/diag_"+std::to_string(i)+".png",px.data(),w,h);
    } return 0;
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
    g.teleportPlayer(v.pos + Vec2{0.0f, 1.6f}, 0);
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
    g.teleportPlayer(t.pos + Vec2{0.0f, 6.0f}, 0);
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
    for (int i = 0; i < 1800 && cops == 0; ++i) { b.idle(1); cops = g.aliveCops(); }
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
    for (int round = 0; round < 6 && g.wantedLevel() < 3; ++round) {
      int t = -1; float best = 1e9f;
      for (auto& n : g.npcs()) {
        if (n.interior || n.state == NpcState::Dead || n.despawn) continue;
        float d = (n.pos - p.pos).length();
        if (d < best) { best = d; t = n.id; }
      }
      if (t < 0) break;
      Npc& n = g.npcs()[t];
      if (best > 12.0f) { g.teleportPlayer(n.pos + Vec2{0.0f, 5.0f}, 0); b.idle(2); }
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
      if (bd > 14.0f && bd < 1e8f) {
        float yaw = g.camera().yaw();
        Vec2 f{std::sin(yaw), -std::cos(yaw)}, rt{std::cos(yaw), std::sin(yaw)}, n = (tgt - p.pos).normalized();
        InputFrame mv; mv.move = {n.dot(rt), n.dot(f)}; mv.runHeld = true;
        b.step(mv, 1);
      } else b.idle(1);
      ++frames;
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
    b.render = false; // simulation uses real input and physics; screenshots render selected states
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
    CHECK(g.focusValid() && g.focus()->kind == IKind::Door, "door interaction available");
    b.shot("20_market_door");
    in = InputFrame();
    in.interactPressed = true;
    b.step(in, 1);
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
