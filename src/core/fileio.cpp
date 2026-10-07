#include "fileio.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#endif

namespace gtabr {
namespace fileio {
static std::string g_root = "assets";
static std::string g_saveDir = ".";
#ifdef __ANDROID__
static AAssetManager* g_mgr = nullptr;
void setAndroidAssetManager(void* m) { g_mgr = (AAssetManager*)m; }
#endif

void setAssetRoot(const std::string& dir) { g_root = dir; }
void setSaveDir(const std::string& dir) { g_saveDir = dir; }
const std::string& saveDir() { return g_saveDir; }

bool readAsset(const std::string& rel, std::vector<uint8_t>& out) {
#ifdef __ANDROID__
  if (!g_mgr) return false;
  AAsset* a = AAssetManager_open(g_mgr, rel.c_str(), AASSET_MODE_STREAMING);
  if (!a) return false;
  off_t len = AAsset_getLength(a);
  out.resize((size_t)len);
  size_t done = 0;
  while (done < (size_t)len) {
    int n = AAsset_read(a, out.data() + done, (size_t)len - done);
    if (n <= 0) break;
    done += (size_t)n;
  }
  AAsset_close(a);
  return done == (size_t)len;
#else
  std::string p = g_root + "/" + rel;
  FILE* f = fopen(p.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize((size_t)len);
  size_t n = fread(out.data(), 1, (size_t)len, f);
  fclose(f);
  return n == (size_t)len;
#endif
}

bool assetExists(const std::string& rel) {
#ifdef __ANDROID__
  if (!g_mgr) return false;
  AAsset* a = AAssetManager_open(g_mgr, rel.c_str(), AASSET_MODE_UNKNOWN);
  if (!a) return false;
  AAsset_close(a);
  return true;
#else
  std::string p = g_root + "/" + rel;
  FILE* f = fopen(p.c_str(), "rb");
  if (!f) return false;
  fclose(f);
  return true;
#endif
}

bool readTextAsset(const std::string& rel, std::string& out) {
  std::vector<uint8_t> d;
  if (!readAsset(rel, d)) return false;
  out.assign((const char*)d.data(), d.size());
  return true;
}

bool writeFileAtomic(const std::string& path, const void* data, size_t size) {
  std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  size_t n = fwrite(data, 1, size, f);
  fflush(f);
  fclose(f);
  if (n != size) return false;
  return rename(tmp.c_str(), path.c_str()) == 0;
}

bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}
}  // namespace fileio
}  // namespace gtabr
