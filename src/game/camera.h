// Camera rig with exactly two views: Top Down and Third Person. Switching interpolates the focus, yaw, pitch,
// distance (zoom) and field of view of one orbit camera, so the change is a single smooth move.
#pragma once
#include "../core/math.h"
#include "world.h"

namespace gtabr {

enum class CamMode { TopDown = 0, ThirdPerson = 1 };

struct CameraInput {
  Vec3 focus;               // world point to follow (feet of the player or centre of the vehicle)
  float headingYaw = 0;     // heading of the followed entity
  Vec2 velocity;            // m/s (look-ahead)
  float speed = 0;
  bool driving = false;
  bool indoors = false;
  Vec2 look;                // right-hand drag delta in pixels-ish units (x = yaw, y = pitch)
  float zoomDelta = 0;      // pinch delta (+ = zoom in)
  bool userDragging = false;
  float shake = 0;
};

class CameraRig {
 public:
  void init(CamMode m);
  void setMode(CamMode m);
  void toggle() { setMode(mode_ == CamMode::TopDown ? CamMode::ThirdPerson : CamMode::TopDown); }
  CamMode mode() const { return mode_; }
  void update(float dt, const CameraInput& in, const World& w, float aspect);
  void snapTo(const CameraInput& in, const World& w, float aspect);

  float blend() const { return blendEased_; }        // 0 = top down, 1 = third person
  float pitchDeg() const { return pitch_ / kDeg2Rad; }
  float yaw() const { return yaw_; }
  const Vec3& eye() const { return eye_; }
  const Mat4& view() const { return view_; }
  const Mat4& proj() const { return proj_; }
  const Mat4& viewProj() const { return viewProj_; }
  const Frustum& frustum() const { return frustum_; }
  Vec3 right() const { return right_; }
  Vec3 up() const { return up_; }
  Vec3 forward() const { return fwd_; }
  Vec3 focus() const { return focus_; }
  float fov() const { return fov_; }
  float topDownZoom() const { return tdDist_; }
  void setTopDownZoom(float z) { tdDist_ = clamp(z, 14.0f, 52.0f); }
  float sensitivity = 1.0f;
  bool invertY = false;

 private:
  CamMode mode_ = CamMode::TopDown;
  float blend_ = 0, blendEased_ = 0;
  // per-mode state
  float tdYaw_ = 0, tdDist_ = 25.0f;
  float tdSpeedZoom_ = 0;      // smoothed extra distance from vehicle speed
  float tdPitchLift_ = 0;      // extra pitch to look over buildings that hide the focus
  float tdIdle_ = 0;
  float tpYaw_ = 0, tpPitch_ = 16.0f * kDeg2Rad, tpDist_ = 5.4f;
  float idleTimer_ = 0;
  // outputs
  Vec3 focus_, eye_, right_, up_, fwd_;
  float yaw_ = 0, pitch_ = 1.2f, dist_ = 30, fov_ = 40 * kDeg2Rad;
  Mat4 view_, proj_, viewProj_;
  Frustum frustum_;
  Vec3 smoothFocus_;
  Vec2 lookAhead_;
  float tpHitDist_ = 99.0f;
  bool first_ = true;
  void compute(const CameraInput& in, const World& w, float aspect, float dt);
};

}  // namespace gtabr
