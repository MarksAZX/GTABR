#include "ui.h"

#include <cstring>

namespace gtabr {

void UiPainter::begin(gfx::FrameData* fd, const Assets* a, float w, float h, float scale, bool highContrast) {
  fd_ = fd; a_ = a; w_ = w; h_ = h; scale_ = scale;highContrast_=highContrast;
  cur_ = gfx::TexHandle{};
  fd_->ui.clear();
  fd_->uiBatches.clear();
}

void UiPainter::end() {
  if (!fd_->uiBatches.empty()) fd_->uiBatches.back().count = (uint32_t)fd_->ui.size() - fd_->uiBatches.back().first;
}

void UiPainter::setTex(gfx::TexHandle t) {
  if (!fd_->uiBatches.empty() && cur_.id == t.id) return;
  if (!fd_->uiBatches.empty()) fd_->uiBatches.back().count = (uint32_t)fd_->ui.size() - fd_->uiBatches.back().first;
  gfx::Batch b;
  b.tex = t;
  b.first = (uint32_t)fd_->ui.size();
  fd_->uiBatches.push_back(b);
  cur_ = t;
}

gfx::UiInst& UiPainter::push(float x, float y, float w, float h, gfx::UiKind kind, Color c) {
  fd_->ui.emplace_back();
  gfx::UiInst& u = fd_->ui.back();
  std::memset(&u, 0, sizeof(u));
  u.rect[0] = x; u.rect[1] = y; u.rect[2] = w; u.rect[3] = h;
  if(highContrast_ && (c>>24)>0){
    bool text = kind==gfx::kUiText;
    int brightness=((c&255)+((c>>8)&255)+((c>>16)&255))/3;
    if(text || brightness<48)c=(c&0x00ffffffu)|0xf5000000u;
  }
  u.color = c; u.color2 = c;
  u.kind = (float)kind;
  return u;
}

void UiPainter::rect(float x, float y, float w, float h, Color c, float radius, float border, Color borderCol) {
  if ((c >> 24) == 0 && border <= 0) return;
  setTex(a_->iconsTex);
  gfx::UiInst& u = push(x, y, w, h, gfx::kUiRect, c);
  u.radius = radius;
  u.p0 = border;
  u.color2 = borderCol;
}

void UiPainter::gradient(float x, float y, float w, float h, Color top, Color bottom, float radius) {
  setTex(a_->iconsTex);
  gfx::UiInst& u = push(x, y, w, h, gfx::kUiGradient, top);
  u.color2 = bottom;
  u.radius = radius;
}

void UiPainter::glow(float x, float y, float w, float h, float radius, float blur, Color c) {
  setTex(a_->iconsTex);
  gfx::UiInst& u = push(x - blur, y - blur, w + 2 * blur, h + 2 * blur, gfx::kUiGlow, c);
  u.radius = radius + blur;
  u.p0 = blur;
}

void UiPainter::circle(float cx, float cy, float r, Color c, float border, Color borderCol) {
  rect(cx - r, cy - r, r * 2, r * 2, c, r, border, borderCol);
}

void UiPainter::arc(float cx, float cy, float rIn, float rOut, float a0, float a1, Color c) {
  setTex(a_->iconsTex);
  gfx::UiInst& u = push(cx - rOut - 2, cy - rOut - 2, (rOut + 2) * 2, (rOut + 2) * 2, gfx::kUiArc, c);
  u.p0 = rIn; u.p1 = rOut; u.p2 = a0; u.p3 = a1;
}

void UiPainter::icon(const char* name, float cx, float cy, float size, Color c) {
  UvRect r = a_->icon(name);
  if (!r.valid) return;
  image(a_->iconsTex, r, cx - size * 0.5f, cy - size * 0.5f, size, size, c, 0);
}

void UiPainter::image(gfx::TexHandle tex, const UvRect& uv, float x, float y, float w, float h, Color c, float radius) {
  if (!uv.valid) return;
  setTex(tex);
  gfx::UiInst& u = push(x, y, w, h, gfx::kUiImage, c);
  u.uv[0] = uv.u0; u.uv[1] = uv.v0; u.uv[2] = uv.u1; u.uv[3] = uv.v1;
  u.radius = radius;
}

void UiPainter::art(const char* name, float x, float y, float w, float h, Color c, float radius) {
  image(a_->artTex, a_->art(name), x, y, w, h, c, radius);
}

void UiPainter::map(gfx::TexHandle tex, float cu, float cv, float uvScale, float rot, float x, float y, float w, float h, float radius, Color c) {
  setTex(tex);
  gfx::UiInst& u = push(x, y, w, h, gfx::kUiMap, c);
  u.uv[0] = cu; u.uv[1] = cv; u.uv[2] = uvScale; u.uv[3] = rot;
  u.radius = radius;
}

float UiPainter::textWidth(bool bold, const std::string& s, float size) const {
  const FontData& f = bold ? a_->fontBold : a_->fontRegular;
  float sc = size / f.baseSize, wsum = 0;
  for (uint32_t cp : Assets::decodeUtf8(s)) {
    auto it = f.glyphs.find(cp);
    if (it == f.glyphs.end()) it = f.glyphs.find('?');
    if (it != f.glyphs.end()) wsum += it->second.adv * sc;
  }
  return wsum;
}

float UiPainter::lineHeight(bool bold, float size) const {
  const FontData& f = bold ? a_->fontBold : a_->fontRegular;
  return (f.ascent + f.descent) * size / f.baseSize;
}

float UiPainter::text(bool bold, const std::string& s, float x, float y, float size, Color c, Align al, Color outline, float outlineW) {
  const FontData& f = bold ? a_->fontBold : a_->fontRegular;
  float width = textWidth(bold, s, size);
  if (al == Align::Center) x -= width * 0.5f;
  else if (al == Align::Right) x -= width;
  if ((c >> 24) == 0) return width;
  setTex(f.tex);
  float sc = size / f.baseSize, cx = x;
  for (uint32_t cp : Assets::decodeUtf8(s)) {
    auto it = f.glyphs.find(cp);
    if (it == f.glyphs.end()) it = f.glyphs.find('?');
    if (it == f.glyphs.end()) continue;
    const Glyph& g = it->second;
    if (cp != ' ') {
      gfx::UiInst& u = push(cx - f.spread * sc, y - f.spread * sc, f.cell * sc, f.cell * sc, gfx::kUiText, c);
      u.uv[0] = g.u0; u.uv[1] = g.v0; u.uv[2] = g.u1; u.uv[3] = g.v1;
      u.p0 = outlineW;
      u.color2 = outline;
    }
    cx += g.adv * sc;
  }
  return width;
}

}  // namespace gtabr
