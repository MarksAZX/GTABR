#include "game.h"
#include <algorithm>
#include <sstream>
namespace gtabr {
void Game::subtitle(const std::string& text,const std::string& speaker,bool sound){
  if(sound?!settings_.soundCaptions:!settings_.subtitles)return;
  subtitleSound_=sound;
  subtitleText_=text;subtitleSpeaker_=speaker;
  subtitleUntil_=realTime_+clamp(2.0f+text.size()*0.035f,3.0f,7.0f);
}
void Game::drawSubtitles(){
  if((subtitleSound_?!settings_.soundCaptions:!settings_.subtitles)||realTime_>=subtitleUntil_||panel_.open||menu_!=MenuState::None)return;
  float S=std::min(uiScale(),screenH_/900.0f),w=std::min(screenW_*0.55f,850*S);
  std::string caption=subtitleSpeaker_.empty()?subtitleText_:subtitleSpeaker_+": "+subtitleText_;
  std::istringstream words(caption);std::string word,line;std::vector<std::string> lines;
  while(words>>word){std::string next=line.empty()?word:line+" "+word;
    if(!line.empty()&&ui_.textWidth(false,next,20*S)>w-32*S){lines.push_back(line);line=word;}else line=next;
  }
  if(!line.empty())lines.push_back(line);
  float height=(28*lines.size()+28)*S,x=(screenW_-w)/2,y=screenH_-110*S-height;
  ui_.rect(x,y,w,height,rgba(0.01f,0.015f,0.02f,0.93f),6*S);
  for(size_t i=0;i<lines.size();++i)ui_.text(false,lines[i],x+16*S,y+(14+28*i)*S,20*S,0xffffffffu);
}
void Game::drawControlEditor(){
  float S=std::min(uiScale(),screenH_/900.0f);
  ui_.rect(0,0,screenW_,screenH_,rgba(0.025f,0.03f,0.035f,0.72f));
  ui_.text(true,"POSICIONAR CONTROLES",screenW_/2,28*S,32*S,0xffffffffu,Align::Center);
  ui_.text(false,"Arraste cada controle. O joystick movido passa a ser fixo.",screenW_/2,75*S,20*S,rgba(1,1,1,0.75f),Align::Center);
  auto layout=makeLayout();
  const HudButton* buttons[]={&layout.run,&layout.interact,&layout.enterExit,&layout.camera,&layout.wheel,&layout.pause,&layout.attack,&layout.reload};
  const char* names[]={"Correr","Interagir","Veículo","Câmera","Armas","Pausa","Atacar","Recarregar","Joystick"};
  for(int i=0;i<9;++i){Vec2 center=i==8?layout.joyCenter:buttons[i]->c;float radius=i==8?layout.joyRadius:buttons[i]->r;
    ui_.circle(center.x,center.y,radius,rgba(0.12f,0.17f,0.20f,i==controlDrag_?0.95f:0.8f),2*S,rgba(0.8f,0.88f,0.92f));
    ui_.text(false,names[i],center.x,center.y-10*S,18*S,0xffffffffu,Align::Center);
    uiRects_.push_back({Vec4(center.x-radius,center.y-radius,radius*2,radius*2),800+i});
  }
  float y=screenH_-62*S;
  for(int i=0;i<2;++i){float x=i?screenW_-265*S:32*S;ui_.rect(x,y,230*S,46*S,rgba(0.08f,0.10f,0.12f,0.98f),6*S,1*S,rgba(1,1,1,0.3f));
    ui_.text(false,i?"Salvar e voltar":"Restaurar padrão",x+16*S,y+10*S,22*S,0xffffffffu);uiRects_.push_back({Vec4(x,y,230*S,46*S),i?304:810});}
}
}
