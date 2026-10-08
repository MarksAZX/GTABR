#include "save_store.h"
#include "fileio.h"
#include <zlib.h>
#include <map>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cerrno>
#include <cstdint>
#include <fstream>
namespace gtabr::save_store {
namespace {
bool candidate(const std::string& path,std::string& data){
  std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return false;
  auto size=file.tellg();if(size<1||size>4*1024*1024)return false;
  data.resize((size_t)size);file.seekg(0);return bool(file.read(data.data(),size));
}
}
bool valid(const std::string& data) {
  if(data.empty() || data.size()>4*1024*1024 || data.back()!='\n')return false;
  std::map<std::string,std::string> kv;std::istringstream in(data);std::string line;bool duplicate=false;
  while(std::getline(in,line)){auto p=line.find('=');if(p==std::string::npos)return false;auto key=line.substr(0,p);duplicate|=kv.count(key)>0;kv[key]=line.substr(p+1);}
  if(kv["version"]!="3"&&kv["version"]!="4"&&kv["version"]!="5")return false;
  for(const char* field:{"money","playerX","playerZ"}){
    if(!kv.count(field))return false;
    char* end=nullptr;double n=std::strtod(kv[field].c_str(),&end);
    if(end==kv[field].c_str()||*end||!std::isfinite(n))return false;
  }
  if(kv["version"]!="3"){
    if(!kv.count("seed"))return false;
    char* end=nullptr;errno=0;
    auto seed=std::strtoull(kv["seed"].c_str(),&end,10);
    if(errno||end==kv["seed"].c_str()||*end||seed>UINT32_MAX||kv["seed"].front()=='-')return false;
  }
  if(kv["version"]=="5"){
    if(duplicate)return false;
    const std::string tag="integrity_crc32=";auto pos=data.rfind(tag);
    if(pos==std::string::npos||pos==0||data[pos-1]!='\n'||data.size()-pos!=tag.size()+9)return false;
    std::string hex=data.substr(pos+tag.size(),8);if(hex.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)return false;
    auto expected=std::strtoul(hex.c_str(),nullptr,16);
    if(crc32(0,(const Bytef*)data.data(),(uInt)pos)!=expected)return false;
  }
  return true;
}
bool read(const std::string& path,std::string& data,bool* recovered){
  if(recovered)*recovered=false;
  if(candidate(path,data)&&valid(data))return true;
  if(candidate(path+".bak",data)&&valid(data)){if(recovered)*recovered=true;return true;}
  data.clear();return false;
}
bool write(const std::string& path,const std::string& payload){
  // Seal complete data before touching either existing file.
  if(payload.find("integrity_crc32=")!=std::string::npos)return false;
  char crc[32];std::snprintf(crc,sizeof(crc),"integrity_crc32=%08lx\n",(unsigned long)crc32(0,(const Bytef*)payload.data(),(uInt)payload.size()));
  std::string sealed=payload+crc;if(!valid(sealed))return false;
  std::string previous;
  if(candidate(path,previous)&&valid(previous)){
    if(!fileio::writeFileAtomic(path+".bak",previous.data(),previous.size()))return false;
  }else if(!candidate(path+".bak",previous)||!valid(previous)){
    if(!fileio::writeFileAtomic(path+".bak",sealed.data(),sealed.size()))return false;
  }
  return fileio::writeFileAtomic(path,sealed.data(),sealed.size());
}
}
