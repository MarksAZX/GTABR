#pragma once
#include "../core/math.h"
#include <cstdint>
namespace gtabr {
struct WeatherState {
  float clock=0,rain=0,wet=0,cloud=0.38f,wind=0.2f;
  void update(float dt,uint32_t seed,int mode){
    dt=clamp(dt,0.0f,0.1f);clock+=dt;
    float phase=std::fmod(clock+float(seed%37),720.0f);
    float target=mode==2?0.95f:mode==1?0.0f:smoothstep(clamp((phase-180)/60,0.0f,1.0f))*(1-smoothstep(clamp((phase-440)/90,0.0f,1.0f)))*0.9f;
    rain+=(target-rain)*expDecay(0.4f,dt);
    wet=clamp(wet+dt*(rain*0.042f-(1-rain)*0.0035f),0.0f,1.0f);
    cloud=clamp(0.38f+rain*0.5f+0.055f*std::sin(clock*0.004f+seed%17),0.25f,0.97f);
    wind=clamp(0.22f+rain*0.65f+0.12f*std::sin(clock*0.017f),0.1f,1.0f);
  }
};
}
