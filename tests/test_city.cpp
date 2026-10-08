#include "game/world.h"
#include "game/model.h"
#include <cstdio>
#include <cstring>
using namespace gtabr;
uint64_t hash(const World& w){uint64_t h=1469598103934665603ull;auto bytes=[&](const void*p,size_t n){for(size_t i=0;i<n;++i){h^=((const uint8_t*)p)[i];h*=1099511628211ull;}};
  for(const auto& c:w.chunks){bytes(c.mesh.v.data(),c.mesh.v.size()*sizeof(gfx::WorldVertex));bytes(c.mesh.idx.data(),c.mesh.idx.size()*4);}
  std::vector<uint8_t> map;renderMinimap(w,map,128,w.half);bytes(map.data(),map.size());return h;
}
int main(){bool sides[5]={};uint64_t prev=0;
  for(uint32_t seed=1;seed<=24;++seed){World a,b;buildWorld(a,seed);buildWorld(b,seed);auto ha=hash(a),hb=hash(b);
    if(ha!=hb||ha==prev||a.shops.size()!=4||a.interiors.size()!=4||a.roads.empty()||!a.playArea.contains(a.spawnPlayer.x,a.spawnPlayer.z))return 1;
    prev=ha;sides[a.coastSide+1]=true;
    for(const auto& sh:a.shops){if(sh.stock.empty()||sh.interior<0||sh.interior>=4||a.interiorAt(sh.clerk.x,sh.clerk.z)!=sh.interior)return 2;}
    for(const auto& c:a.chunks)for(const auto& v:c.mesh.v){for(float p:v.p)if(!std::isfinite(p))return 3;if(v.layer==31&&v.n[1]<0)return 4;}
  }
  for(bool side:sides)if(!side)return 5;
  puts("24 seeds: deterministic meshes/maps, four functioning shop definitions, all coast directions, finite geometry, upward water normals");return 0;
}
