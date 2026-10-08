#include "game/input.h"
#include <cstdio>
using namespace gtabr;
int main(){
  InputLayout l;l.width=960;l.height=540;l.joyFixed=true;l.joyRadius=50;l.joyCenter={650,140};
  HudButton* buttons[]={&l.run,&l.interact,&l.enterExit,&l.camera,&l.wheel,&l.pause,&l.attack,&l.reload};
  for(auto* b:buttons)b->visible=false;
  InputSystem i;i.poll(l);i.onTouch(0,TouchAction::Down,650,140);auto f=i.poll(l);
  if(!f.joyActive||(f.joyBase-l.joyCenter).length()>0.1f)return 1;
  i.onTouch(0,TouchAction::Move,685,140);f=i.poll(l);if(f.move.x<0.5f||std::fabs(f.move.y)>0.01f)return 2;
  i.onTouch(0,TouchAction::Up,685,140);i.poll(l);
  l.run={{200,100},32,true};i.poll(l);i.onTouch(1,TouchAction::Down,200,100);f=i.poll(l);if(!f.runHeld)return 3;
  i.onTouch(1,TouchAction::Up,200,100);if(i.poll(l).runHeld)return 4;
  i.onTouch(2,TouchAction::Down,900,430);i.poll(l);i.onTouch(2,TouchAction::Move,880,400);f=i.poll(l);if(!f.lookDragging||f.joyActive)return 5;
  i.reset();l.joyFixed=false;l.leftHanded=true;l.joyZoneRight=350;i.poll(l);
  i.onTouch(3,TouchAction::Down,800,400);f=i.poll(l);if(!f.joyActive)return 6;
  i.reset();l.modal=true;i.poll(l);i.onTouch(4,TouchAction::Down,200,100);f=i.poll(l);if(f.runHeld||f.joyActive||f.ui.empty())return 7;
  puts("custom fixed joystick, relocated button, camera area, left-handed layout, and modal input passed");return 0;
}
