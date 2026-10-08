#include "../core/util.h"
#include "../core/fileio.h"
#include "decor_models.h"
#include "meshbuilder.h"
#include "material_ids.h"
#include <algorithm>
#include <map>
namespace gtabr {
namespace {
void tube(MeshBuilder& b,Vec3 a,Vec3 c,float r0,float r1,int sides,int layer){
  Vec3 axis=(c-a).normalized(),u=axis.cross(std::fabs(axis.y)>0.9f?Vec3{1,0,0}:Vec3{0,1,0}).normalized(),v=axis.cross(u);
  for(int i=0;i<sides;++i){float t=i*kTau/sides,t1=(i+1)*kTau/sides;Vec3 x=u*std::cos(t)+v*std::sin(t),z=u*std::cos(t1)+v*std::sin(t1);
    b.quad(a+x*r0,a+z*r0,c+z*r1,c+x*r1,{0,0},{1,0},{1,1},{0,1},layer);}
}
void crown(MeshBuilder& b,Vec3 center,Vec3 size,int sides,int rings){
  auto point=[&](float t,float p){
    float phase=center.x*1.7f+center.z*2.1f;
    float lobes=1+0.13f*std::sin(3*p+phase)*std::sin(t)+0.08f*std::cos(5*p-2*t+phase);
    return center+Vec3{std::sin(t)*std::cos(p)*size.x*lobes,std::cos(t)*size.y+0.08f*size.y*std::sin(p+phase)*std::sin(t),std::sin(t)*std::sin(p)*size.z*lobes};};
  for(int j=0;j<rings;++j)for(int i=0;i<sides;++i){float t=0.02f+(kPi-0.04f)*j/rings,t1=0.02f+(kPi-0.04f)*(j+1)/rings,p=i*kTau/sides,p1=(i+1)*kTau/sides;
    b.quad(point(t,p),point(t,p1),point(t1,p1),point(t1,p),{0,0},{1,0},{1,1},{0,1},mat::foliage,0.88f,0.88f,1,1);
    auto& vertices=b.mesh()->v;for(size_t k=vertices.size()-4;k<vertices.size();++k){auto& v=vertices[k];Vec3 q{(v.p[0]-center.x)/(size.x*size.x),(v.p[1]-center.y)/(size.y*size.y),(v.p[2]-center.z)/(size.z*size.z)};q=q.normalized();v.n[0]=int8_t(q.x*127);v.n[1]=int8_t(q.y*127);v.n[2]=int8_t(q.z*127);}
  }
}
MeshData tree(int species,int variant,int lod){
  MeshData m;MeshBuilder b(&m);Rng rng(1234+species*173+variant*91);int sides=lod==0?8:5;
  bool palm=species==3||species==4;float h=species==5?0.7f:(palm?(species==4?8.8f:6.8f):4.5f);
  b.setTint({0.32f,0.23f,0.15f});Vec3 tip{palm?0.6f:0.15f,h,0.15f};
  Vec3 bend=tip*0.48f+Vec3{0.12f*std::sin(float(variant+species)),0,0.18f};
  tube(b,{0,0,0},bend,species==5?0.13f:0.30f,0.16f,sides,mat::wood);
  tube(b,bend,tip,0.16f,0.065f,sides,mat::wood);
  if(palm){
    b.setTint({0.28f,0.43f,0.18f});int leaves=lod==2?5:9;
    for(int i=0;i<leaves;++i){float a=i*kTau/leaves+variant*0.4f;Vec3 dir{std::cos(a),0,std::sin(a)},side{-dir.z,0,dir.x};
      Vec3 p0=tip,p1=tip+dir*1.3f+Vec3{0,0.9f,0},p2=tip+dir*2.7f+Vec3{0,-0.4f,0};
      b.quad(p0,p1+side*0.5f,p2,p1-side*0.5f,{0,0},{1,0},{1,1},{0,1},mat::foliage);
      if(lod==0)for(int j=0;j<4;++j){Vec3 p=lerp(p1,p2,j/4.0f);tube(b,p,p+side*0.8f+Vec3{0,-0.3f,0},0.08f,0.01f,3,mat::foliage);}
    }
  }else{
    Vec3 colors[]={ {0.24f,0.43f,0.19f},{0.30f,0.46f,0.20f},{0.24f,0.38f,0.22f},{0.2f,0.4f,0.2f},{0.2f,0.4f,0.2f},{0.3f,0.46f,0.23f},{0.23f,0.40f,0.22f},{0.25f,0.42f,0.19f} };
    int count=lod==2?1:(species==5?3:9);float spread=species==5?0.7f:(species==6||species==7?2.9f:2.1f);
    for(int i=0;i<count;++i){float angle=i*kTau/count+variant*0.5f;Vec3 end=tip+Vec3{std::cos(angle)*spread*rng.range(0.35f,0.8f),rng.range(-0.7f,0.65f),std::sin(angle)*spread*rng.range(0.35f,0.8f)};
      if(lod==2)end=tip;
      b.setTint({0.32f,0.23f,0.15f});if(lod==0)tube(b,{0,h*0.65f,0},end,0.12f,0.045f,5,mat::wood);
      b.setTint(colors[species]*rng.range(0.85f,1.10f));float r=count==1?spread:spread*rng.range(0.40f,0.62f);
      crown(b,end,{r,species==6?0.65f:r*0.85f,r*rng.range(0.8f,1.1f)},lod==0?11:8,lod==0?6:4);
      if(lod==0)for(int leaf=0;leaf<10;++leaf){float a=leaf*kTau/10;Vec3 p=end+Vec3{std::cos(a)*r*0.9f,rng.range(-0.35f,0.45f)*r,std::sin(a)*r*0.9f};Vec3 side{-std::sin(a)*0.24f,0,std::cos(a)*0.24f};
        b.quad(p-side,p+Vec3{0,0.34f,0},p+side,p-Vec3{0,0.28f,0},{0,0.5f},{0.5f,0},{1,0.5f},{0.5f,1},mat::foliage);}
      // Flowering trees retain a green canopy; blooms are sparse clusters, not a solid coloured ball.
      if(lod==0&&(species==1||species==2||species==7)&&i%3==0){
        b.setTint(species==1?Vec3{0.72f,0.64f,0.23f}:(species==2?Vec3{0.52f,0.35f,0.56f}:Vec3{0.68f,0.31f,0.19f}));
        crown(b,end+Vec3{r*0.7f,r*0.35f,0},{r*0.25f,r*0.22f,r*0.3f},6,3);
      }
    }
  }return m;
}
MeshData prop(int kind,int lod){
  MeshData m;MeshBuilder b(&m);int sides=lod==0?10:6;
  auto box=[&](Vec3 a,Vec3 z,Vec3 color,int layer=mat::metal){b.setTint(color);b.box(AABB(a,z),layer,layer,1);};
  auto pole=[&](Vec3 a,Vec3 z,float r,Vec3 col){b.setTint(col);tube(b,a,z,r,r*0.8f,sides,mat::metal);};
  Vec3 steel{0.28f,0.30f,0.32f},wood{0.53f,0.36f,0.23f},white{0.86f,0.86f,0.81f};
  switch(kind){
    case 0:case 13: b.setTint(kind==0?Vec3{0.29f,0.38f,0.33f}:Vec3{0.32f,0.43f,0.48f});b.prism({},0.38f,0.9f,sides,mat::metal);b.setTint(steel);b.prism({0,0.91f,0},0.42f,0.08f,sides,mat::metal);break;
    case 1:
      for(float x:{-0.8f,0.8f}){pole({x,0,-0.25f},{x,0.52f,-0.25f},0.055f,steel);pole({x,0,0.25f},{x,0.95f,0.25f},0.055f,steel);}
      for(int i=0;i<4;++i)box({-1,0.48f,-0.3f+i*0.16f},{1,0.55f,-0.18f+i*0.16f},wood,mat::wood);
      for(int i=0;i<3;++i)box({-1,0.68f+i*0.14f,0.25f},{1,0.78f+i*0.14f,0.31f},wood,mat::wood);
      break;
    case 2:
      pole({}, {0,6.0f,0},0.13f,steel);pole({0,6,0},{0,6.9f,-0.7f},0.075f,steel);pole({0,6.9f,-0.7f},{0,7,-1.7f},0.06f,steel);box({-0.2f,6.9f,-2.05f},{0.2f,7.12f,-1.5f},white);break;
    case 3:
      box({-0.43f,0,-0.32f},{0.43f,0.7f,0.32f},{0.25f,0.37f,0.3f});box({-0.46f,0.7f,-0.33f},{0.46f,1.55f,0.33f},white);box({-0.3f,1.10f,-0.35f},{0.3f,1.40f,-0.34f},{0.07f,0.11f,0.1f});
      pole({0.5f,1.35f,0},{0.72f,0.55f,0},0.035f,steel);pole({0.72f,0.55f,0},{0.5f,0.8f,0},0.035f,steel);break;
    case 4: pole({}, {0,1.3f,0},0.06f,steel);box({-0.46f,1.05f,-0.10f},{0.46f,1.95f,0.36f},{0.33f,0.48f,0.52f});box({-0.30f,1.13f,-0.13f},{0.30f,1.8f,-0.11f},{0.12f,0.14f,0.15f});pole({0.18f,1.65f,-0.18f},{0.18f,1.38f,-0.18f},0.045f,white);break;
    case 5: pole({}, {0,0.65f,0},0.16f,{0.7f,0.22f,0.13f});pole({-0.25f,0.42f,0},{0.25f,0.42f,0},0.07f,steel);break;
    case 6: b.setTint({0.78f,0.39f,0.12f});tube(b,{0,0.06f,0},{0,0.75f,0},0.28f,0.025f,sides,mat::white);box({-0.32f,0,-0.32f},{0.32f,0.06f,0.32f},steel);break;
    case 7: {
      pole({}, {0,2.25f,0},0.04f,white);
      for(int i=0;i<10;++i){float a=i*kTau/10,z=(i+1)*kTau/10;b.setTint(i%2?Vec3{0.79f,0.72f,0.55f}:Vec3{0.28f,0.43f,0.49f});
        b.quad({0,2.3f,0},{std::cos(a)*1.5f,1.85f,std::sin(a)*1.5f},{std::cos(z)*1.5f,1.85f,std::sin(z)*1.5f},{std::cos(z)*1.5f,1.85f,std::sin(z)*1.5f},{0,0},{1,0},{1,1},{0,1},mat::white);}break;
    }
    case 8: for(float x:{-0.32f,0.32f}){pole({x,0,-0.4f},{x,0.48f,0.1f},0.025f,white);pole({x,0,0.4f},{x,0.95f,0.4f},0.025f,white);}
      box({-0.33f,0.38f,-0.35f},{0.33f,0.44f,0.35f},{0.32f,0.47f,0.54f});box({-0.33f,0.43f,0.34f},{0.33f,0.98f,0.4f},white);break;
    case 9:case 15:
      for(float x:{-1.2f,1.2f})for(float z:{-0.8f,0.8f})pole({x,0,z},{x,kind==15?3.7f:2.6f,z},0.09f,wood);
      box({-1.3f,kind==15?2.1f:0.5f,-0.9f},{1.3f,kind==15?2.22f:1.12f,0.9f},wood,mat::wood);
      b.setTint({0.6f,0.36f,0.22f});b.gableRoof(-1.6f,-1.2f,1.6f,1.2f,kind==15?3.7f:2.6f,0.65f,true,mat::wood,1,0.1f);
      if(kind==15)for(int i=0;i<7;++i)box({-0.42f,i*0.3f,-1.1f},{0.42f,i*0.3f+0.07f,-0.9f},wood,mat::wood);
      break;
    case 10: b.setTint({0.63f,0.36f,0.23f});tube(b,{0,0,0},{0,0.65f,0},0.22f,0.38f,sides,mat::white);b.setTint({0.23f,0.4f,0.2f});crown(b,{0,0.95f,0},{0.45f,0.5f,0.45f},sides,3);break;
    case 11:box({-0.4f,0,-0.35f},{0.4f,0.55f,0.35f},wood,mat::wood);box({-0.30f,0.55f,-0.25f},{0.3f,1,0.3f},white,mat::wood);break;
    case 12:for(int i=0;i<3;++i){b.setTint({0.12f,0.12f,0.12f});b.prism({0,i*0.25f,0},0.48f,0.2f,sides,mat::metal);}break;
    case 16: box({-0.5f,0,-0.24f},{0.5f,0.65f,0.24f},white);for(int i=0;i<6;++i)box({-0.44f,0.08f+i*0.09f,-0.255f},{0.44f,0.11f+i*0.09f,-0.25f},steel);break;
    case 17: b.setTint({0.37f,0.46f,0.22f});for(int i=0;i<15;++i){float a=i*2.4f;Vec3 p{std::sin(a)*0.32f,0,std::cos(a)*0.32f},tip=p+Vec3{std::sin(a)*0.15f,0.3f+(i%4)*0.08f,std::cos(a)*0.15f};b.quad(p-Vec3{0.045f,0,0},p+Vec3{0.045f,0,0},tip,tip,{0,1},{1,1},{0.5f,0},{0.5f,0},mat::foliage);}break;
    case 18: box({-0.7f,0,-0.32f},{0.7f,0.45f,0.32f},{0.47f,0.43f,0.36f},mat::concrete);b.setTint({0.2f,0.32f,0.16f});for(float x:{-0.45f,0.0f,0.45f})crown(b,{x,0.6f,0},{0.30f,0.31f,0.28f},sides,3);break;
    case 19: pole({}, {0,0.85f,0},0.08f,steel);box({-0.09f,0.65f,-0.09f},{0.09f,0.73f,0.09f},white);break;
    case 20: for(float x:{-1.4f,1.4f})pole({x,0,0.35f},{x,2.6f,0.35f},0.06f,steel);box({-1.65f,2.6f,-0.8f},{1.65f,2.72f,0.65f},steel);box({-1.35f,0.9f,0.32f},{1.35f,2.3f,0.36f},{0.33f,0.44f,0.47f});for(int i=0;i<3;++i)box({-1.2f,0.48f,-0.25f+i*0.16f},{1.2f,0.56f,-0.12f+i*0.16f},wood,mat::wood);break;
    case 21: box({-0.28f,0,-0.2f},{0.28f,0.35f,0.2f},{0.22f,0.35f,0.48f});box({-0.30f,0.35f,-0.22f},{0.30f,0.41f,0.22f},white);break;
    case 14:pole({}, {0,2.5f,0},0.035f,steel);box({-0.65f,2.1f,-0.04f},{0.65f,2.48f,0.04f},{0.2f,0.35f,0.3f});break;
  }return m;
}
}
void buildDecorModels(gfx::Renderer& r,DecorModels& out){
  struct Part{std::vector<gfx::ModelVertex> v;std::vector<uint32_t> idx;gfx::ModelLod lod[3];};
  std::vector<Part> parts;std::map<uint64_t,int> colors;std::vector<uint32_t> palette;std::vector<int> surfaces;
  auto add=[&](bool isTree,int kind,int variant){Part p;
    for(int lod=0;lod<3;++lod){MeshData m=isTree?tree(kind,variant,lod):prop(kind,lod);p.lod[lod]={(uint32_t)p.idx.size(),(uint32_t)m.idx.size()};uint32_t base=p.v.size();
      for(const auto&w:m.v){gfx::ModelVertex v{};std::copy(w.p,w.p+3,v.p);std::copy(w.n,w.n+4,v.n);
        Vec3 n{w.n[0]/127.0f,w.n[1]/127.0f,w.n[2]/127.0f};Vec3 t=n.cross(std::fabs(n.y)>0.9f?Vec3{0,0,1}:Vec3{0,1,0}).normalized();v.t[0]=(int8_t)(t.x*127);v.t[1]=(int8_t)(t.y*127);v.t[2]=(int8_t)(t.z*127);v.t[3]=127;
        uint32_t color=w.color|0xff000000;uint64_t key=(uint64_t(w.layer)<<32)|color;int c;v.n[3]=w.layer==mat::foliage?127:0;
        auto it=colors.find(key);if(it==colors.end()){c=palette.size();if(c>=512){float best=1e9f;c=0;for(size_t k=0;k<palette.size();++k){float d=0;for(int ch=0;ch<3;++ch){float delta=float((color>>(ch*8))&255)-float((palette[k]>>(ch*8))&255);d+=delta*delta;}if(surfaces[k]!=w.layer)d+=100000;if(d<best){best=d;c=k;}}}else{palette.push_back(color);surfaces.push_back(w.layer);colors[key]=c;}}else c=it->second;
        v.uv[0]=(c%32+0.12f+0.76f*clamp(w.uv[0],0.0f,1.0f))/32;v.uv[1]=(c/32+0.12f+0.76f*clamp(w.uv[1],0.0f,1.0f))/16;p.v.push_back(v);
      }for(auto index:m.idx)p.idx.push_back(base+index);
    }parts.push_back(std::move(p));};
  for(int i=0;i<8;++i)for(int v=0;v<3;++v)add(true,i,v);
  for(int i=0;i<22;++i)add(false,i,0);
  constexpr int width=768,height=384,cell=24;
  std::vector<uint8_t> leaf;fileio::readAsset("data/decor_leaf.rgba",leaf);
  std::vector<uint8_t> tex(width*height*4),orm(tex.size()),normal(tex.size());
  for(int y=0;y<height;++y)for(int x=0;x<width;++x){int c=(y/cell)*32+x/cell;uint32_t col=c<(int)palette.size()?palette[c]:0xffffffff;int surface=c<(int)surfaces.size()?surfaces[c]:mat::white;size_t p=(y*width+x)*4;
    float grain=0.94f+0.06f*((x*19+y*31)%11)/10;
    if(surface==mat::foliage&&leaf.size()==64*64*4){size_t q=(((y%cell)*64/cell)*64+(x%cell)*64/cell)*4;grain=0.65f+0.65f*(leaf[q]+leaf[q+1]+leaf[q+2])/(3*160.0f);}
    if(surface==mat::wood)grain*=0.90f+0.10f*std::sin(x*1.2f+y*0.18f);
    for(int k=0;k<3;++k)tex[p+k]=(uint8_t)clamp(float((col>>(k*8))&255)*grain,0.0f,255.0f);
    tex[p+3]=255;orm[p]=255;orm[p+1]=surface==mat::metal?105:surface==mat::wood?170:215;orm[p+2]=surface==mat::metal?100:0;orm[p+3]=255;
    normal[p]=128+(x%5-2)*2;normal[p+1]=128+(y%5-2)*2;normal[p+2]=255;normal[p+3]=255;
  }
  auto t=r.createTextureRGBA(width,height,tex.data(),true,false,gfx::SamplerKind::ClampLinear);
  auto n=r.createTextureRGBA(width,height,normal.data(),false,false,gfx::SamplerKind::ClampLinear);
  auto o=r.createTextureRGBA(width,height,orm.data(),false,false,gfx::SamplerKind::ClampLinear);
  out.material=r.createModelMaterial(t,n,o);
  for(size_t i=0;i<parts.size();++i){auto&p=parts[i];auto handle=r.createModel(p.v.data(),p.v.size(),p.idx.data(),p.idx.size(),p.lod,3,false);if(i<24)out.trees.push_back(handle);else out.props.push_back(handle);}
  out.ready=true;
}
}
