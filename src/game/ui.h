// Immediate-mode 2D painter that emits UiInst batches (rounded rects, arcs, icons, SDF text, minimap).
#pragma once
#include <string>

#include "assets.h"

namespace gtabr {

using Color = uint32_t;
inline Color rgba(float r, float g, float b, float a = 1.0f) { return packRGBA8(r, g, b, a); }
inline Color rgba8(int r, int g, int b, int a = 255) { return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24); }
inline Color withAlpha(Color c, float a) {
  uint32_t oa = (c >> 24) & 0xFF;
  uint32_t na = (uint32_t)clamp(oa * a, 0.0f, 255.0f);
  return (c & 0x00FFFFFFu) | (na << 24);
}
inline Color mixColor(Color a, Color b, float t) {
  auto ch = [&](int s) { return (uint32_t)clamp(((a >> s) & 0xFF) * (1 - t) + ((b >> s) & 0xFF) * t, 0.0f, 255.0f); };
  return ch(0) | (ch(8) << 8) | (ch(16) << 16) | (ch(24) << 24);
}

enum class Align { Left, Center, Right };

class UiPainter {
 public:
  void begin(gfx::FrameData* fd, const Assets* a, float w, float h, float scale);
  void end();
  float width() const { return w_; }
  float height() const { return h_; }
  float S(float v) const { return v * scale_; }
  float scale() const { return scale_; }

  void rect(float x, float y, float w, float h, Color c, float radius = 0, float border = 0, Color borderCol = 0);
  void gradient(float x, float y, float w, float h, Color top, Color bottom, float radius = 0);
  void glow(float x, float y, float w, float h, float radius, float blur, Color c);
  void circle(float cx, float cy, float r, Color c, float border = 0, Color borderCol = 0);
  // angles in radians, 0 = up, clockwise
  void arc(float cx, float cy, float rIn, float rOut, float a0, float a1, Color c);
  void icon(const char* name, float cx, float cy, float size, Color c);
  void image(gfx::TexHandle tex, const UvRect& uv, float x, float y, float w, float h, Color c = 0xFFFFFFFFu, float radius = 0);
  void art(const char* name, float x, float y, float w, float h, Color c = 0xFFFFFFFFu, float radius = 0);
  void map(gfx::TexHandle tex, float cu, float cv, float uvScale, float rot, float x, float y, float w, float h, float radius, Color c);
  float text(bool bold, const std::string& s, float x, float y, float size, Color c, Align al = Align::Left, Color outline = 0,
             float outlineW = 0);
  float textWidth(bool bold, const std::string& s, float size) const;
  float lineHeight(bool bold, float size) const;

 private:
  void setTex(gfx::TexHandle t);
  gfx::UiInst& push(float x, float y, float w, float h, gfx::UiKind kind, Color c);
  gfx::FrameData* fd_ = nullptr;
  const Assets* a_ = nullptr;
  float w_ = 0, h_ = 0, scale_ = 1;
  gfx::TexHandle cur_;
};

}  // namespace gtabr
