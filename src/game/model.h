// Runtime 3D models (gmesh) and skeletal animation (ganim): loading, pose sampling/blending, procedural layers,
// skinning palettes. Characters share the Meshy skeleton, so every clip plays on every character.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "../core/math.h"
#include "../gfx/gmesh.h"
#include "../gfx/renderer.h"

namespace gtabr {

struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
  static Quat axisAngle(Vec3 axis, float a) {
    float s = std::sin(a * 0.5f);
    Vec3 n = axis.normalized();
    return {n.x * s, n.y * s, n.z * s, std::cos(a * 0.5f)};
  }
  Quat operator*(const Quat& b) const {
    return {w * b.x + x * b.w + y * b.z - z * b.y, w * b.y - x * b.z + y * b.w + z * b.x, w * b.z + x * b.y - y * b.x + z * b.w,
            w * b.w - x * b.x - y * b.y - z * b.z};
  }
  float dot(const Quat& b) const { return x * b.x + y * b.y + z * b.z + w * b.w; }
  Quat normalized() const {
    float l = std::sqrt(x * x + y * y + z * z + w * w);
    return l > 1e-8f ? Quat{x / l, y / l, z / l, w / l} : Quat{};
  }
};
// Normalised lerp along the shortest arc (good enough between 30 Hz samples and for blending).
inline Quat nlerp(const Quat& a, Quat b, float t) {
  if (a.dot(b) < 0) b = {-b.x, -b.y, -b.z, -b.w};
  return Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}.normalized();
}

struct Xform {
  Vec3 t;
  Quat r;
  Vec3 s{1, 1, 1};
  Mat4 matrix() const;
};
Mat4 quatMatrix(const Quat& q);
// Character / vehicle placement: model space faces -Z; yaw follows forwardFromYaw().
Mat4 yawMatrix(Vec3 pos, float yaw);
Mat4 inverseGeneral(const Mat4& a);

struct Skeleton {
  std::vector<std::string> names;
  std::vector<int> parent;      // parents always precede children
  std::vector<Mat4> invBind;
  std::vector<Xform> rest;
  int find(const std::string& n) const;
};

struct AnimClip {
  std::string name;
  int frames = 0;
  float fps = 30, duration = 1;
  std::vector<std::string> boneNames;
  std::vector<Xform> keys;      // frames * bones
  float groundSpeed = 0;        // m/s the clip's feet travel at rate 1 (stride matching)
};

struct ModelAsset {
  std::string name;
  bool ok = false;
  bool skinned = false, vehicle = false;
  gfx::ModelHandle gpu;
  gfx::MaterialHandle material;
  Skeleton skel;
  std::vector<std::vector<int>> clipMap;  // [clip][bone] -> clip bone index or -1
  AABB bounds;
  float lodDistance[3] = {20, 60, 200};
  int lodCount = 1;
  uint32_t lodTris[3] = {};
  float animRootScale = 1.0f;
  float armDown[2] = {0, 0};        // idle: rotation (about Z) bringing the A-pose arms down
  Mat4 restHand;                    // right-hand bone frame in the rest pose (weapon grips are authored against it)
  Mat4 rootFix;                     // maps the rig space (exporter units/axes) onto the mesh space
  // vehicles (model space)
  Vec3 wheel[4];
  float wheelRadius = 0.3f, wheelWidth = 0.2f;
  Vec3 headlight[2], taillight[2];
  // CPU data kept until upload
  std::vector<gfx::ModelVertex> verts;
  std::vector<uint32_t> idx;
  std::vector<gfx::ModelLod> lods;
  std::string texAlbedo, texNormal, texOrm;
};

bool loadModel(const std::string& path, ModelAsset& out);
bool loadClip(const std::string& path, AnimClip& out);
// Creates GPU buffers and the material from already uploaded textures. Frees CPU arrays.
void uploadModel(gfx::Renderer& r, ModelAsset& m, gfx::TexHandle albedo, gfx::TexHandle normal, gfx::TexHandle orm);
void bindClips(ModelAsset& m, const std::vector<AnimClip>& clips);
// Estimates the ground speed of an in-place locomotion clip from planted-foot sliding.
float estimateGroundSpeed(const Skeleton& sk, const Mat4& rootFix, const AnimClip& c, const std::vector<int>& map);

// Procedural wheel (tyre + rim) unit mesh: axis along X, radius 1, width 1 centred on the origin.
void buildWheelMesh(std::vector<gfx::ModelVertex>& v, std::vector<uint32_t>& idx);
// 256x128 albedo (left tyre, right rim) and matching ORM texture data.
void buildWheelTextures(std::vector<uint8_t>& albedo, std::vector<uint8_t>& orm, uint32_t& w, uint32_t& h);

enum ClipId { kClipIdle = 0, kClipWalk = 1, kClipRun = 2, kClipSwim = 3, kClipSwimIdle = 4, kClipCount = 5 };
// One-shot action clips (Meshy library, same skeleton), stored after the locomotion clips.
enum ActionId { kActPunch = 0, kActKick, kActHit, kActKnockDown, kActStandUp, kActSlash, kActReload, kActChat, kActCount };
inline const char* actionFile(int a) {
  static const char* k[kActCount] = {"punch", "kick", "hit", "knockdown", "standup", "slash", "reload", "chat"};
  return k[a];
}

// Per-character animation state: locomotion blend tree (idle/walk/run by real speed, stride matched) plus
// procedural layers applied in model space (talk gestures, reach/interact, refuel, crouch into a car, turn lean, head look).
struct CharAnim {
  float t[kClipCount] = {0, 0, 0, 0, 0};   // clip time (s)
  float w[kClipCount] = {1, 0, 0, 0, 0};   // smoothed weights
  bool swimming = false;                   // locomotion uses the swim / tread-water clips
  float rateScale = 1.0f;                // per-NPC variation
  float talk = 0, reach = 0, refuel = 0, crouch = 0, wave = 0;
  float lean = 0, headYaw = 0;
  float talkTarget = 0, reachTarget = 0, refuelTarget = 0, crouchTarget = 0, waveTarget = 0;
  float leanTarget = 0, headYawTarget = 0;
  float gestureClock = 0;
  // action slot (one-shot clip over locomotion; upper = arms/torso only) with a crossfade from the previous action
  int action = -1, prevAction = -1;
  float actT = 0, actSpeed = 1, actW = 0, prevT = 0, prevW = 0;
  bool actUpper = false, actHold = false, prevUpper = false, actFading = false;
  // aiming (procedural arms toward the aim direction) + recoil
  float aim = 0, aimTarget = 0, recoil = 0;
  bool twoHanded = false;
  float aimPitch = 0;
  // right hand frame in model space for held weapons
  Vec3 handPos, handDir, handSide;
  Mat4 handMat;                          // full right-hand bone frame in model space (weapons follow its rotation)
  bool handValid = false;
  float accum = 0;                       // reduced-rate update accumulator
  bool valid = false;
  std::array<Mat4, gfx::kMaxBones> palette;
};

class Animator {
 public:
  void init(const std::vector<AnimClip>* clips) { clips_ = clips; }
  // Advances state with the character's real ground speed and evaluates the skinning palette.
  void update(CharAnim& a, const ModelAsset& m, float speed, float dt, bool evaluate);
  const AnimClip* clip(int i) const { return clips_ && i < (int)clips_->size() ? &(*clips_)[i] : nullptr; }
  // Starts a one-shot action. speed scales playback; hold keeps the last frame (knock-down) until another action.
  void play(CharAnim& a, int action, float speed = 1.0f, bool upperBody = false, bool hold = false) const;
  void stop(CharAnim& a) const { a.actFading = true; }
  float actionDuration(int action) const;
  bool hasAction(int action) const;

 private:
  void evaluate(CharAnim& a, const ModelAsset& m);
  const std::vector<AnimClip>* clips_ = nullptr;
  std::vector<Xform> pose_, tmp_;
  std::vector<Mat4> global_;
};

}  // namespace gtabr
