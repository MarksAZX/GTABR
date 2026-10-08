#include "game.h"
namespace gtabr {
void Game::spawnTraffic(){
  if(world_.roads.empty())return;
  Rng random(uint64_t(world_.seed)^0x315ac82);
  for(int tries=0;tries<120&&vehicles_.size()<11;++tries){
    const auto& road=world_.roads[random.irange(0,int(world_.roads.size())-1)];
    float along=random.range(road.a+8,road.b-8);int sign=random.chance(0.5f)?1:-1;
    float lane=std::min(2.2f,road.hw*0.45f);
    Vec2 pos=road.horizontal?Vec2{along,road.c+sign*lane}:Vec2{road.c-sign*lane,along};
    if((pos-Vec2{world_.spawnPlayer.x,world_.spawnPlayer.z}).length()<32)continue;
    bool occupied=false;for(const auto& v:vehicles_)if((v.pos-pos).length()<12)occupied=true;
    for(const auto& c:world_.colliders)if(c.box.mn.y<1.5f&&pos.x>c.box.mn.x-1.2f&&pos.x<c.box.mx.x+1.2f&&pos.y>c.box.mn.z-2.5f&&pos.y<c.box.mx.z+2.5f)occupied=true;
    if(occupied)continue;
    Vehicle v;v.id=vehicles_.size();v.model=random.irange(0,2);v.color=random.irange(0,4);v.ambientTraffic=true;v.pos=pos;v.yaw=road.horizontal?sign*kPi*0.5f:sign>0?kPi:0;v.fuel=vehicleDef(v.model).fuelCap;v.engineOn=true;
    v.aiTarget=road.horizontal?Vec2{sign>0?road.b-5:road.a+5,pos.y}:Vec2{pos.x,sign>0?road.b-5:road.a+5};vehicles_.push_back(v);
  }
}
void Game::updateTraffic(float dt){
  if(player_.indoors)return;
  int active=0;const int budget[]={2,4,6,8};
  for(auto& v:vehicles_){if(!v.ambientTraffic||v.despawn)continue;
    if((v.pos-player_.pos).length()>preset().drawDistance*1.25f||active>=budget[clamp(settings_.quality,0,3)])continue;
    ++active;
    Vec2 forward=fwd2(v.yaw);bool blocked=false;
    auto ahead=[&](Vec2 p,float radius){Vec2 d=p-v.pos;float longitudinal=d.dot(forward);return longitudinal>-1&&longitudinal<radius&&std::fabs(d.dot(right2(v.yaw)))<2.0f;};
    if(player_.vehicle<0&&ahead(player_.pos,9))blocked=true;
    for(const auto& other:vehicles_)if(other.id!=v.id&&!other.despawn&&ahead(other.pos,10))blocked=true;
    for(const auto& n:npcs_)if(!n.interior&&!n.despawn&&ahead(n.pos,8))blocked=true;
    if(blocked){v.aiReverse=0;v.aiStuck=0;driveAi(v,v.pos,0,dt);continue;}
    if((v.aiTarget-v.pos).length()<9||v.aiStuck>4){
      const auto& road=world_.roads[(v.id*7+int(weather_.clock/30))%world_.roads.size()];
      float midpoint=(road.a+road.b)*0.5f;v.aiTarget=road.horizontal?Vec2{midpoint,road.c}:Vec2{road.c,midpoint};v.aiStuck=0;
    }
    driveAi(v,v.aiTarget,5.5f+v.id%3,dt);
  }
}
}
