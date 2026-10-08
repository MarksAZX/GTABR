#include "game/weather.h"
#include <cstdio>
using namespace gtabr;
int main(){WeatherState a,b;float maxRain=0;
 for(int frame=0;frame<24000;++frame){a.update(1.0f/30,7,0);b.update(1.0f/30,7,0);maxRain=std::max(maxRain,a.rain);
 if(a.clock!=b.clock||a.rain!=b.rain||a.wet!=b.wet||!std::isfinite(a.wind)||a.wet<0||a.wet>1)return 1;}
 if(maxRain<0.8f)return 2;
 WeatherState rain;for(int f=0;f<1500;++f)rain.update(1.0f/30,1,2);if(rain.rain<0.9f||rain.wet<0.8f)return 3;
 float wet=rain.wet;for(int f=0;f<1500;++f)rain.update(1.0f/30,1,1);if(rain.rain>0.01f||rain.wet>=wet)return 4;
 WeatherState restored=rain;for(int f=0;f<100;++f){restored.update(1.0f/30,1,0);rain.update(1.0f/30,1,0);}if(restored.wet!=rain.wet||restored.rain!=rain.rain)return 5;
 puts("weather deterministic cycle, real rainfall, wetting/drying, restored evolution passed");return 0;}
