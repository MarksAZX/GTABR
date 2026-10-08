// Platform-independent asset/file access. Android reads from the APK via AAssetManager.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gtabr {
namespace fileio {
// Installs the asset root (desktop: directory path) or Android asset manager.
void setAssetRoot(const std::string& dir);
#ifdef __ANDROID__
void setAndroidAssetManager(void* aassetManager);
#endif
void setSaveDir(const std::string& dir);
const std::string& saveDir();

bool readAsset(const std::string& relPath, std::vector<uint8_t>& out);
bool assetExists(const std::string& relPath);
bool readTextAsset(const std::string& relPath, std::string& out);

bool writeFileAtomic(const std::string& absPath, const void* data, size_t size);
bool readFile(const std::string& absPath, std::string& out);
bool removeFile(const std::string& absPath);
bool fileExists(const std::string& absPath);
}  // namespace fileio
}  // namespace gtabr
