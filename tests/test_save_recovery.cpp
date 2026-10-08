#include "core/save_store.h"
#include "core/fileio.h"
#include <filesystem>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <poll.h>
using namespace gtabr;
int main(){
  char tmp[]="/tmp/gtabr-save-XXXXXX";if(!mkdtemp(tmp))return 1;
  std::string path=std::string(tmp)+"/slot_0.sav";
  auto payload=[](int money){return "version=5\nseed=4294967295\nmoney="+std::to_string(money)+"\nplayerX=12.25\nplayerZ=-35.5\n";};
  std::string data;bool recovered=false;
  if(!save_store::write(path,payload(100))||!save_store::write(path,payload(200)))return 2;
  fileio::writeFileAtomic(path,"version=5\n",10);
  if(!save_store::read(path,data,&recovered)||!recovered||data.find("money=100\n")==std::string::npos)return 3;
  // An invalid primary must never overwrite its valid backup during the next save.
  if(!save_store::write(path,payload(300)))return 4;
  std::string backup;fileio::readFile(path+".bak",backup);if(backup.find("money=100\n")==std::string::npos)return 5;
  data.back()='x';if(save_store::valid(data))return 6;
  std::string oversized(5*1024*1024,'x');fileio::writeFileAtomic(path,oversized.data(),oversized.size());
  if(!save_store::read(path,data,&recovered)||!recovered)return 15;
  if(!save_store::write(path,payload(300)))return 16;
  for(bool backing:{true,false})for(auto stage:{fileio::AtomicStage::TempSynced,fileio::AtomicStage::Renamed}){
    if(!save_store::write(path,payload(400)))return 7;
    int ready[2];if(pipe(ready))return 8;pid_t child=fork();if(child<0)return 9;
    if(child==0){close(ready[0]);fileio::setAtomicWriteHook([&](const std::string& target,fileio::AtomicStage point){
      if(target==(backing?path+".bak":path)&&point==stage){char signal='1';write(ready[1],&signal,1);for(;;)pause();}
    });save_store::write(path,payload(500));_exit(10);}
    close(ready[1]);pollfd event{ready[0],POLLIN,0};
    if(poll(&event,1,5000)!=1){kill(child,SIGKILL);waitpid(child,nullptr,0);return 10;}
    char signal=0;if(read(ready[0],&signal,1)!=1)return 10;close(ready[0]);
    kill(child,SIGKILL);int status=0;waitpid(child,&status,0);
    if(!WIFSIGNALED(status)||WTERMSIG(status)!=SIGKILL||!save_store::read(path,data,&recovered))return 11;
    std::string expected=(!backing&&stage==fileio::AtomicStage::Renamed)?"money=500\n":"money=400\n";
    if(data.find(expected)==std::string::npos)return 12;
    fileio::readFile(path+".bak",backup);if(!save_store::valid(backup))return 17;
  }
  std::string legacy="version=4\nseed=1\nmoney=400\nplayerX=2\nplayerZ=3\nhealth=90\nhealth=90\n";
  if(!save_store::valid(legacy))return 13;
  fileio::writeFileAtomic(path,"broken",6);fileio::writeFileAtomic(path+".bak","broken",6);
  if(save_store::read(path,data))return 14;
  std::filesystem::remove_all(tmp);
  puts("save CRC, backup recovery, legacy migration, and SIGKILL before/after rename passed");return 0;
}
