// Minimal math library: Vec2/3/4, Mat4 (column-major, Vulkan clip space), AABB, helpers.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace gtabr {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;
constexpr float kDeg2Rad = kPi / 180.0f;

template <class T> constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clamp(v, 0.0f, 1.0f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float t) { t = saturate(t); return t * t * (3.0f - 2.0f * t); }
inline float smootherstep(float t) { t = saturate(t); return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float sign(float v) { return v < 0.0f ? -1.0f : 1.0f; }
// Frame-rate independent exponential smoothing factor.
inline float expDecay(float rate, float dt) { return 1.0f - std::exp(-rate * dt); }
inline float wrapAngle(float a) {
  a = std::fmod(a + kPi, kTau);
  if (a < 0) a += kTau;
  return a - kPi;
}
inline float angleDiff(float from, float to) { return wrapAngle(to - from); }
inline float lerpAngle(float a, float b, float t) { return a + angleDiff(a, b) * t; }

struct Vec2 {
  float x = 0, y = 0;
  constexpr Vec2() = default;
  constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
  Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(float s) const { return {x * s, y * s}; }
  Vec2 operator/(float s) const { return {x / s, y / s}; }
  Vec2 operator-() const { return {-x, -y}; }
  Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
  Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
  Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
  float dot(Vec2 o) const { return x * o.x + y * o.y; }
  float lengthSq() const { return x * x + y * y; }
  float length() const { return std::sqrt(lengthSq()); }
  Vec2 normalized() const { float l = length(); return l > 1e-8f ? *this / l : Vec2{0, 0}; }
  Vec2 perp() const { return {-y, x}; }
};
inline Vec2 operator*(float s, Vec2 v) { return v * s; }

struct Vec3 {
  float x = 0, y = 0, z = 0;
  constexpr Vec3() = default;
  constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  Vec3 operator-() const { return {-x, -y, -z}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
  float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
  Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
  float lengthSq() const { return dot(*this); }
  float length() const { return std::sqrt(lengthSq()); }
  Vec3 normalized() const { float l = length(); return l > 1e-8f ? *this / l : Vec3{0, 0, 0}; }
  Vec2 xz() const { return {x, z}; }
};
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }

struct Vec4 {
  float x = 0, y = 0, z = 0, w = 0;
  constexpr Vec4() = default;
  constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
};

// World yaw convention: yaw 0 faces -Z (north); +yaw turns clockwise seen from above (toward +X).
inline Vec3 forwardFromYaw(float yaw) { return {std::sin(yaw), 0.0f, -std::cos(yaw)}; }
inline Vec3 rightFromYaw(float yaw) { return {std::cos(yaw), 0.0f, std::sin(yaw)}; }
inline Vec2 fwd2(float yaw) { return {std::sin(yaw), -std::cos(yaw)}; }
inline Vec2 right2(float yaw) { return {std::cos(yaw), std::sin(yaw)}; }
inline float yawFromDir(Vec2 d) { return std::atan2(d.x, -d.y); }

struct Mat4 {
  float m[16];  // column-major: m[col*4 + row]
  Mat4() { setIdentity(); }
  void setIdentity() {
    std::memset(m, 0, sizeof(m));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
  }
  float& at(int row, int col) { return m[col * 4 + row]; }
  float at(int row, int col) const { return m[col * 4 + row]; }
  Mat4 operator*(const Mat4& o) const {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
      for (int rr = 0; rr < 4; ++rr) {
        float s = 0;
        for (int k = 0; k < 4; ++k) s += at(rr, k) * o.at(k, c);
        r.at(rr, c) = s;
      }
    return r;
  }
  Vec4 operator*(const Vec4& v) const {
    return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3) * v.w,
            at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3) * v.w,
            at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3) * v.w,
            at(3, 0) * v.x + at(3, 1) * v.y + at(3, 2) * v.z + at(3, 3) * v.w};
  }
  Vec3 transformPoint(const Vec3& p) const {
    Vec4 r = (*this) * Vec4(p, 1.0f);
    return {r.x / r.w, r.y / r.w, r.z / r.w};
  }
  static Mat4 translation(const Vec3& t) {
    Mat4 r;
    r.at(0, 3) = t.x; r.at(1, 3) = t.y; r.at(2, 3) = t.z;
    return r;
  }
  // Right-handed look-at (camera looks down -Z in view space).
  static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    Vec3 f = (target - eye).normalized();
    Vec3 s = f.cross(up).normalized();
    Vec3 u = s.cross(f);
    Mat4 r;
    r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z; r.at(0, 3) = -s.dot(eye);
    r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z; r.at(1, 3) = -u.dot(eye);
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z; r.at(2, 3) = f.dot(eye);
    return r;
  }
  // Vulkan clip space: y down, z in [0,1].
  static Mat4 perspective(float fovyRad, float aspect, float zn, float zf) {
    float t = 1.0f / std::tan(fovyRad * 0.5f);
    Mat4 r;
    std::memset(r.m, 0, sizeof(r.m));
    r.at(0, 0) = t / aspect;
    r.at(1, 1) = -t;
    r.at(2, 2) = zf / (zn - zf);
    r.at(2, 3) = (zf * zn) / (zn - zf);
    r.at(3, 2) = -1.0f;
    return r;
  }
  static Mat4 ortho(float l, float r_, float b, float t, float zn, float zf) {
    Mat4 r;
    r.at(0, 0) = 2.0f / (r_ - l);
    r.at(1, 1) = -2.0f / (t - b);  // y down in clip space
    r.at(2, 2) = 1.0f / (zn - zf);
    r.at(0, 3) = -(r_ + l) / (r_ - l);
    r.at(1, 3) = (t + b) / (t - b);
    r.at(2, 3) = zn / (zn - zf);
    return r;
  }
};

struct AABB {
  Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
  AABB() = default;
  AABB(const Vec3& a, const Vec3& b) : mn(a), mx(b) {}
  void expand(const Vec3& p) {
    mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
    mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
  }
  void expand(const AABB& b) { expand(b.mn); expand(b.mx); }
  Vec3 center() const { return (mn + mx) * 0.5f; }
  Vec3 extent() const { return (mx - mn) * 0.5f; }
  bool overlapsXZ(const AABB& o) const { return mn.x <= o.mx.x && mx.x >= o.mn.x && mn.z <= o.mx.z && mx.z >= o.mn.z; }
  bool overlaps(const AABB& o) const { return overlapsXZ(o) && mn.y <= o.mx.y && mx.y >= o.mn.y; }
  bool containsXZ(float x, float z) const { return x >= mn.x && x <= mx.x && z >= mn.z && z <= mx.z; }
};

struct Plane { Vec3 n; float d; float dist(const Vec3& p) const { return n.dot(p) + d; } };
struct Frustum {
  Plane planes[6];
  void fromViewProj(const Mat4& vp);
  bool intersects(const AABB& b) const;
  bool intersectsSphere(const Vec3& c, float r) const;
};

inline void Frustum::fromViewProj(const Mat4& vp) {
  // Gribb/Hartmann, Vulkan depth [0,1].
  auto row = [&](int i) { return Vec4(vp.at(i, 0), vp.at(i, 1), vp.at(i, 2), vp.at(i, 3)); };
  Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
  Vec4 p[6] = {
      {r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w}, {r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w},
      {r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w}, {r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w},
      {r2.x, r2.y, r2.z, r2.w},                            {r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w}};
  for (int i = 0; i < 6; ++i) {
    float l = std::sqrt(p[i].x * p[i].x + p[i].y * p[i].y + p[i].z * p[i].z);
    planes[i] = {Vec3(p[i].x / l, p[i].y / l, p[i].z / l), p[i].w / l};
  }
}
inline bool Frustum::intersects(const AABB& b) const {
  for (const Plane& pl : planes) {
    Vec3 v{pl.n.x >= 0 ? b.mx.x : b.mn.x, pl.n.y >= 0 ? b.mx.y : b.mn.y, pl.n.z >= 0 ? b.mx.z : b.mn.z};
    if (pl.dist(v) < 0) return false;
  }
  return true;
}
inline bool Frustum::intersectsSphere(const Vec3& c, float r) const {
  for (const Plane& pl : planes)
    if (pl.dist(c) < -r) return false;
  return true;
}

inline uint32_t packRGBA8(float r, float g, float b, float a = 1.0f) {
  auto q = [](float v) { return (uint32_t)(saturate(v) * 255.0f + 0.5f); };
  return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
}
inline uint32_t packRGBA8(const Vec3& c, float a = 1.0f) { return packRGBA8(c.x, c.y, c.z, a); }

}  // namespace gtabr
