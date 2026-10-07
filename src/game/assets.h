// Asset loading: GTEX textures (async decode on worker threads, GPU upload on the main thread), sprite index,
// SDF font metrics, icon / UI art atlases.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/jobs.h"
#include "../gfx/renderer.h"

namespace gtabr {

struct SpriteDef {
  gfx::TexHandle tex;
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  float pivX = 0.5f, pivY = 1.0f;  // pivot as a fraction of the sprite rectangle
  float wm = 1, hm = 1;            // size in metres
  bool valid = false;
};

struct Glyph { float u0 = 0, v0 = 0, u1 = 0, v1 = 0, adv = 0; bool valid = false; };
struct FontData {
  gfx::TexHandle tex;
  float baseSize = 56, ascent = 0, descent = 0, spread = 9, cell = 84;
  std::unordered_map<uint32_t, Glyph> glyphs;
};

struct UvRect { float u0 = 0, v0 = 0, u1 = 0, v1 = 0; bool valid = false; };

class Assets {
 public:
  // Starts asynchronous loading of every runtime texture/metadata file.
  void startLoading(gfx::Renderer* r, JobSystem* jobs);
  // Call each frame on the main thread; uploads finished textures. Returns true once everything is ready.
  bool pump();
  float progress() const;
  bool failed() const { return failed_; }

  const SpriteDef* sprite(const std::string& name) const;
  UvRect icon(const std::string& name) const;
  UvRect art(const std::string& name) const;

  gfx::TexHandle materials, materialsNormal, iconsTex, artTex, mapTex;
  FontData fontRegular, fontBold;

  // Measures / helpers for text
  static std::vector<uint32_t> decodeUtf8(const std::string& s);

 private:
  struct Pending {
    std::string key;
    gfx::TextureData data;
    gfx::SamplerKind sampler;
    bool ok = false;
  };
  void queueTexture(const std::string& key, const std::string& file, gfx::SamplerKind sampler);
  void onTexture(const Pending& p);
  void parseSprites(const std::string& text);
  void parseFont(const std::string& text, FontData& f);
  void parseRects(const std::string& text, char tag, std::unordered_map<std::string, UvRect>& out);

  gfx::Renderer* r_ = nullptr;
  JobSystem* jobs_ = nullptr;
  std::mutex m_;
  std::vector<Pending> done_;
  std::atomic<int> total_{0}, finished_{0};
  bool failed_ = false;
  std::unordered_map<std::string, gfx::TexHandle> pageTex_;  // "atlas_page" -> texture
  std::unordered_map<std::string, SpriteDef> sprites_;
  std::unordered_map<std::string, UvRect> icons_, art_;
  std::string spriteText_;
  bool spritesParsed_ = false;
  bool metaLoaded_ = false;
};

}  // namespace gtabr
