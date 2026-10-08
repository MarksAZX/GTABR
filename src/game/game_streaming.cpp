#include "game.h"
#include <algorithm>
namespace gtabr {
void Game::releaseWorld() {
  if(!worldInstalled_) return;
  for(auto& c:world_.chunks) {r_->destroyMesh(c.handle);r_->destroyMesh(c.lodHandle);}
  r_->destroyMesh(world_.interiorCeilingHandle);
  r_->destroyTexture(mapTex_); mapTex_={};
  worldInstalled_=false;
}
void Game::updateStreaming() {
  if(!worldInstalled_)return;
  Vec3 focus=cam_.focus();
  float range=preset().drawDistance*settings_.renderDistance;
  lodDistance_=range*0.58f;
  struct Request{float dist;World::Chunk* chunk;bool detail;};
  std::vector<Request> todo;
  for(auto& c:world_.chunks) {
    Vec3 center=c.bounds.center();float dx=center.x-focus.x,dz=center.z-focus.z;
    float dist=std::sqrt(dx*dx+dz*dz);
    // A margin and hysteresis prefetch the next sectors and avoid thrashing at boundaries.
    bool eligible=c.interior==player_.indoors;
    bool near=eligible&&dist<lodDistance_+World::kChunk*1.5f;
    bool mid=eligible&&dist<range+World::kChunk*2;
    if(c.handle.valid()&&(!eligible||dist>lodDistance_+World::kChunk*2.5f)){r_->destroyMesh(c.handle);c.handle={};}
    if(c.lodHandle.valid()&&(!eligible||dist>range+World::kChunk*3)){r_->destroyMesh(c.lodHandle);c.lodHandle={};}
    if(near&&!c.handle.valid()&&!c.mesh.empty())todo.push_back({dist,&c,true});
    if(mid&&!c.lodHandle.valid()&&!c.lod.empty())todo.push_back({dist+30,&c,false});
    // Interiors and detailed-only sectors need a fallback within the visible range.
    if(mid&&c.lod.empty()&&!c.handle.valid()&&!c.mesh.empty()&&!near)todo.push_back({dist,&c,true});
  }
  std::sort(todo.begin(),todo.end(),[](const Request&a,const Request&b){return a.dist<b.dist;});
  size_t bytes=0;int uploads=0;
  for(const auto& req:todo){
    auto& c=*req.chunk;auto& mesh=req.detail?c.mesh:c.lod;
    size_t size=mesh.v.size()*sizeof(gfx::WorldVertex)+mesh.idx.size()*sizeof(uint32_t);
    if(uploads>=3||(uploads&&bytes+size>1024*1024))break;
    auto h=r_->createMesh(mesh.v.data(),mesh.v.size(),mesh.idx.data(),mesh.idx.size());
    if(req.detail)c.handle=h;else c.lodHandle=h;
    bytes+=size;++uploads;
  }
}
}
