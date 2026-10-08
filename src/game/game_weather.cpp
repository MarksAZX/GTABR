#include "game.h"
#include <algorithm>
namespace gtabr {
void Game::updateWeather(float dt){
  weather_.update(dt,world_.seed,settings_.weatherMode);cloudCover_=weather_.cloud;wetness_=weather_.wet;
  waterRings_.forEach([&](WaterRing& ring,int id){ring.age+=dt;if(ring.age>=ring.life)waterRings_.release(id);});
  float depth=world_.waterDepth(player_.pos.x,player_.pos.y);
  bool touching=depth>0.04f&&player_.vehicle<0&&!player_.indoors;
  if(touching!=waterContact_){
    if(settings_.effects){
      for(int i=0;i<14;++i)if(auto* p=particles_.acquire()){
        float a=i*kTau/14;p->pos={player_.pos.x,world_.waterLevel+0.1f,player_.pos.y};
        p->vel={std::cos(a)*(0.8f+i%3*0.2f),1.2f+i%4*0.25f,std::sin(a)*(0.8f+i%3*0.2f)};
        p->life=p->maxLife=0.65f;p->size=0.05f;p->gravity=4.5f;p->color=packRGBA8(0.68f,0.87f,0.90f,1);
      }
    }
    waterContact_=touching;waterStep_=0;
  }
  waterStep_-=dt;
  if(touching&&settings_.effects&&waterStep_<=0){
    if(auto* ring=waterRings_.acquire()){ring->pos={player_.pos.x,world_.waterLevel+0.16f,player_.pos.y};ring->age=0;ring->life=1.8f;ring->strength=player_.swimming?0.55f:0.35f;}
    waterStep_=player_.speed>0.25f?0.27f:1.05f;
  }
}
void Game::emitWeather(){
  if(player_.indoors||!settings_.effects)return;
  waterRings_.forEach([&](WaterRing& ring,int){float t=ring.age/ring.life;float radius=0.22f+t*1.5f;addDecalEllipse(ring.pos,radius,radius,ring.strength*(1-t),0,2);});
  if(weather_.rain<0.025f)return;
  UvRect dot=assets_.icon("dot");if(!dot.valid)return;
  SpriteDef drop;drop.tex=assets_.iconsTex;drop.u0=dot.u0;drop.v0=dot.v0;drop.u1=dot.u1;drop.v1=dot.v1;drop.pivX=drop.pivY=0.5f;drop.valid=true;drop.wm=0.018f;drop.hm=0.65f;
  const int limits[]={64,128,220,320};int count=(int)(limits[clamp(settings_.quality,0,3)]*weather_.rain);
  Vec3 focus=cam_.focus();
  for(int i=0;i<count;++i){uint32_t hash=i*747796405u+world_.seed;hash=(hash^(hash>>16))*2246822519u;
    float x=float(hash%1009)/1009*26-13,z=float((hash>>10)%1013)/1013*26-13;
    float phase=std::fmod(weather_.clock*10+float((hash>>20)%101)*0.17f,15.0f);
    Vec3 p{focus.x+x+weather_.wind*phase*0.2f,focus.y+15-phase,focus.z+z};
    if(p.y<world_.heightAt(p.x,p.z)+0.05f||!cam_.frustum().intersectsSphere(p,0.7f))continue;
    bool covered=false;for(const auto& b:world_.mapBuildings)if(b.contains(p.x,p.z)){covered=true;break;}if(covered)continue;
    addSprite(&drop,p,1,0.30f*weather_.rain,false,packRGBA8(0.63f,0.73f,0.81f,1),false,false);
    if(i%4==0){float t=std::fmod(weather_.clock*2+i*0.37f,1.0f);float r=0.05f+t*0.18f;addDecalEllipse({p.x,world_.heightAt(p.x,p.z)+0.015f,p.z},r,r,0.13f*weather_.rain*(1-t),0,3);}
  }
}
}
