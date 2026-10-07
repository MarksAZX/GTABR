#include "assets.h"

#include <cstring>
#include <sstream>

#include "../core/fileio.h"
#include "../core/log.h"

namespace gtabr {

namespace {
bool parseGtex(const std::vector<uint8_t>& file, gfx::TextureData& out) {
  if (file.size() < 24 || std::memcmp(file.data(), "GTX1", 4) != 0) return false;
  uint32_t hdr[5];
  std::memcpy(hdr, file.data() + 4, 20);
  out.format = (gfx::TexFormat)hdr[0];
  out.width = hdr[1]; out.height = hdr[2]; out.layers = hdr[3]; out.mips = hdr[4];
  out.bytes.assign(file.begin() + 24, file.end());
  return true;
}
}  // namespace

std::vector<uint32_t> Assets::decodeUtf8(const std::string& s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    uint32_t cp;
    int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
    else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
    else { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
    i += n;
    out.push_back(cp);
  }
  return out;
}

void Assets::queueTexture(const std::string& key, const std::string& file, gfx::SamplerKind sampler) {
  total_++;
  bool astc = r_->caps().astcLdr;
  jobs_->submit([this, key, file, sampler, astc]() {
    Pending p;
    p.key = key;
    p.sampler = sampler;
    std::vector<uint8_t> bytes;
    std::string primary = (astc ? "data/" : "generated_rgba/data/") + file;
    std::string secondary = (astc ? "generated_rgba/data/" : "data/") + file;
    if (fileio::readAsset(primary, bytes) || fileio::readAsset(secondary, bytes)) p.ok = parseGtex(bytes, p.data);
    // a compressed texture the device cannot sample is unusable
    if (p.ok && (p.data.format == gfx::TexFormat::ASTC6x6_SRGB || p.data.format == gfx::TexFormat::ASTC6x6_UNORM) && !astc) p.ok = false;
    if (!p.ok) LOGE("Failed to load texture %s", file.c_str());
    std::lock_guard<std::mutex> l(m_);
    done_.push_back(std::move(p));
  });
}

void Assets::startLoading(gfx::Renderer* r, JobSystem* jobs) {
  r_ = r;
  jobs_ = jobs;
  fileio::readTextAsset("data/sprites.txt", spriteText_);
  std::vector<std::string> pages;
  {
    std::istringstream ss(spriteText_);
    std::string line;
    while (std::getline(ss, line))
      if (line.rfind("PAGE ", 0) == 0) {
        std::istringstream ls(line);
        std::string tag, atlas, idx;
        ls >> tag >> atlas >> idx;
        pages.push_back(atlas + "_" + idx);
      }
  }
  queueTexture("materials", "materials.gtex", gfx::SamplerKind::Repeat);
  queueTexture("materials_n", "materials_n.gtex", gfx::SamplerKind::Repeat);
  queueTexture("font_regular", "font_regular.gtex", gfx::SamplerKind::ClampLinear);
  queueTexture("font_bold", "font_bold.gtex", gfx::SamplerKind::ClampLinear);
  queueTexture("icons", "icons.gtex", gfx::SamplerKind::ClampLinear);
  queueTexture("ui_art", "ui_art.gtex", gfx::SamplerKind::ClampLinear);
  for (auto& p : pages) queueTexture("page:" + p, p + ".gtex", gfx::SamplerKind::ClampLinear);

  std::string t;
  if (fileio::readTextAsset("data/font_regular.txt", t)) parseFont(t, fontRegular);
  if (fileio::readTextAsset("data/font_bold.txt", t)) parseFont(t, fontBold);
  if (fileio::readTextAsset("data/icons.txt", t)) parseRects(t, 'I', icons_);
  if (fileio::readTextAsset("data/ui_art.txt", t)) parseRects(t, 'A', art_);
  metaLoaded_ = true;
}

void Assets::onTexture(const Pending& p) {
  if (!p.ok) { failed_ = true; return; }
  gfx::TexHandle h = r_->createTexture(p.data, p.sampler);
  if (!h.valid()) { LOGE("GPU upload failed for %s", p.key.c_str()); failed_ = true; return; }
  if (p.key == "materials") materials = h;
  else if (p.key == "materials_n") materialsNormal = h;
  else if (p.key == "font_regular") fontRegular.tex = h;
  else if (p.key == "font_bold") fontBold.tex = h;
  else if (p.key == "icons") iconsTex = h;
  else if (p.key == "ui_art") artTex = h;
  else if (p.key.rfind("page:", 0) == 0) pageTex_[p.key.substr(5)] = h;
}

bool Assets::pump() {
  std::vector<Pending> batch;
  {
    std::lock_guard<std::mutex> l(m_);
    // upload at most a couple of textures per frame so the loading screen stays alive
    size_t n = std::min<size_t>(done_.size(), 2);
    for (size_t i = 0; i < n; ++i) batch.push_back(std::move(done_[i]));
    done_.erase(done_.begin(), done_.begin() + n);
  }
  for (auto& p : batch) {
    onTexture(p);
    finished_++;
  }
  bool all = finished_.load() >= total_.load() && total_.load() > 0;
  if (all && !spritesParsed_) {
    parseSprites(spriteText_);
    spritesParsed_ = true;
  }
  return all;
}

float Assets::progress() const {
  int t = total_.load();
  return t ? (float)finished_.load() / (float)t : 0.0f;
}

void Assets::parseSprites(const std::string& text) {
  std::istringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.size() < 2 || line[0] != 'S') continue;
    std::istringstream ls(line);
    std::string tag, atlas, name;
    int page;
    SpriteDef d;
    ls >> tag >> atlas >> page >> name >> d.u0 >> d.v0 >> d.u1 >> d.v1 >> d.pivX >> d.pivY >> d.wm >> d.hm;
    auto it = pageTex_.find(atlas + "_" + std::to_string(page));
    if (it == pageTex_.end()) continue;
    d.tex = it->second;
    d.valid = true;
    // The same sprite name exists in the low and high atlases; keep both under distinct keys.
    sprites_[atlas + ":" + name] = d;
  }
  LOGI("Sprites indexed: %zu", sprites_.size());
}

const SpriteDef* Assets::sprite(const std::string& name) const {
  auto it = sprites_.find(name);
  return it == sprites_.end() ? nullptr : &it->second;
}

void Assets::parseFont(const std::string& text, FontData& f) {
  std::istringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    std::istringstream ls(line);
    std::string tag;
    ls >> tag;
    if (tag == "FONT") {
      float w, h;
      ls >> f.baseSize >> f.ascent >> f.descent >> f.spread >> f.cell >> w >> h;
    } else if (tag == "G") {
      uint32_t cp;
      Glyph g;
      ls >> cp >> g.u0 >> g.v0 >> g.u1 >> g.v1 >> g.adv;
      g.valid = true;
      f.glyphs[cp] = g;
    }
  }
}

void Assets::parseRects(const std::string& text, char tag, std::unordered_map<std::string, UvRect>& out) {
  std::istringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.empty() || line[0] != tag) continue;
    std::istringstream ls(line);
    std::string t, name;
    UvRect r;
    ls >> t >> name >> r.u0 >> r.v0 >> r.u1 >> r.v1;
    r.valid = true;
    out[name] = r;
  }
}

UvRect Assets::icon(const std::string& name) const {
  auto it = icons_.find(name);
  return it == icons_.end() ? UvRect{} : it->second;
}
UvRect Assets::art(const std::string& name) const {
  auto it = art_.find(name);
  return it == art_.end() ? UvRect{} : it->second;
}

}  // namespace gtabr
