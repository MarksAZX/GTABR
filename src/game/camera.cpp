#include "camera.h"

#include <cmath>

#include "physics.h"

namespace gtabr {

namespace {
constexpr float kTdPitch = 71.0f * kDeg2Rad;
constexpr float kTdFov = 38.0f * kDeg2Rad;
constexpr float kTpFov = 60.0f * kDeg2Rad;
}  // namespace

void CameraRig::init(CamMode m) {
  mode_ = m;
  blend_ = blendEased_ = (m == CamMode::ThirdPerson) ? 1.0f : 0.0f;
  first_ = true;
}

void CameraRig::setMode(CamMode m) { mode_ = m; }

void CameraRig::snapTo(const CameraInput& in, const World& w, float aspect) {
  blend_ = blendEased_ = (mode_ == CamMode::ThirdPerson) ? 1.0f : 0.0f;
  first_ = true;
  tpYaw_ = in.headingYaw;
  compute(in, w, aspect, 1.0f / 30.0f);
}

void CameraRig::update(float dt, const CameraInput& in, const World& w, float aspect) {
  float target = mode_ == CamMode::ThirdPerson ? 1.0f : 0.0f;
  float step = dt / 0.95f;  // transition duration
  if (blend_ < target) blend_ = std::min(target, blend_ + step);
  else if (blend_ > target) blend_ = std::max(target, blend_ - step);
  blendEased_ = smootherstep(blend_);
  compute(in, w, aspect, dt);
}

void CameraRig::compute(const CameraInput& in, const World& w, float aspect, float dt) {
  float sens = 0.0028f * sensitivity;
  // ---- user input applies to the active mode
  bool dragging = in.userDragging || std::fabs(in.look.x) + std::fabs(in.look.y) > 0.01f;
  isoBlend_ += ((isometric ? 1.0f : 0.0f) - isoBlend_) * expDecay(4.0f, dt);
  if (first_) isoBlend_ = isometric ? 1.0f : 0.0f;
  if (mode_ == CamMode::TopDown) {
    if (isoBlend_ > 0.5f) {
      // isometric: dragging swings the view, and it settles on the nearest of the four classic diagonals
      isoYaw_ += in.look.x * sens;
      if (!(in.userDragging || std::fabs(in.look.x) > 0.01f)) {
        float snap = std::round((isoYaw_ - 0.7853982f) / 1.5707963f) * 1.5707963f + 0.7853982f;
        isoYaw_ = lerpAngle(isoYaw_, snap, expDecay(5.0f, dt));
      }
      isoYaw_ = wrapAngle(isoYaw_);
      tdYaw_ = isoYaw_;
    } else {
      tdYaw_ = wrapAngle(tdYaw_ + in.look.x * sens);
    }
    tdDist_ = clamp(tdDist_ - in.zoomDelta * 0.05f, 14.0f, 52.0f);
  } else {
    tpYaw_ = wrapAngle(tpYaw_ + in.look.x * sens);
    tpPitch_ = clamp(tpPitch_ + in.look.y * sens * (invertY ? -1.0f : 1.0f) * 0.8f, 3.0f * kDeg2Rad, 58.0f * kDeg2Rad);
    tpDist_ = clamp(tpDist_ - in.zoomDelta * 0.01f, 3.4f, 8.5f);
  }
  if (dragging) idleTimer_ = 0; else idleTimer_ += dt;
  // third person automatically swings behind the followed entity when the user is not dragging
  float followRate = in.driving ? 2.4f : 1.5f;
  if (idleTimer_ > (in.driving ? 0.4f : 1.4f) && (in.driving || in.velocity.length() > 0.8f)) {
    float want = in.headingYaw;
    if (in.driving && in.speed < -1.0f) want = wrapAngle(in.headingYaw + kPi);  // looking back while reversing is confusing: keep behind
    if (in.driving && in.speed < -1.0f) want = in.headingYaw;
    tpYaw_ = lerpAngle(tpYaw_, want, expDecay(followRate, dt));
  }
  // top-down: driving slowly turns the view to the travel direction once the user stops rotating it, so the road
  // ahead is always readable; on foot the view stays where the user left it
  if (mode_ == CamMode::TopDown) {
    tdIdle_ = dragging ? 0.0f : tdIdle_ + dt;
    if (in.driving && tdIdle_ > 2.0f && std::fabs(in.speed) > 6.0f)
      tdYaw_ = lerpAngle(tdYaw_, in.headingYaw, expDecay(0.35f, dt));
  }

  // ---- focus with look-ahead
  Vec2 vel = in.velocity;
  float la = in.driving ? 0.55f : 0.25f;
  Vec2 want = vel * la;
  float ml = want.length();
  if (ml > 7.0f) want = want * (7.0f / ml);
  lookAhead_ += (want - lookAhead_) * expDecay(3.0f, dt);
  Vec3 f = in.focus;
  float t = blendEased_;
  Vec2 aheadTd = lookAhead_;
  Vec2 aheadTp = lookAhead_ * 0.15f;
  f.x += lerp(aheadTd.x, aheadTp.x, t);
  f.z += lerp(aheadTd.y, aheadTp.y, t);
  if (first_) smoothFocus_ = f;
  else {
    float k = expDecay(in.driving ? 9.0f : 12.0f, dt);
    smoothFocus_ += (f - smoothFocus_) * k;
  }
  // height offset: aim at the upper body in third person, at the ground when top down
  float headH = in.driving ? 1.1f : 1.45f;
  focus_ = smoothFocus_ + Vec3{0, lerp(0.0f, headH, t), 0};

  // ---- orbit parameters per mode
  // top-down pulls back with speed (smoothed so braking does not snap the view)
  float speedZoom = in.driving ? clamp(std::fabs(in.speed) / 30.0f, 0.0f, 1.0f) * 0.85f : (in.speed > 4.0f ? 0.12f : 0.0f);
  tdSpeedZoom_ += (speedZoom - tdSpeedZoom_) * expDecay(speedZoom > tdSpeedZoom_ ? 1.2f : 0.6f, dt);
  if (first_) tdSpeedZoom_ = speedZoom;
  // isometric: a long lens (narrow fov) from further back, which flattens perspective like a true isometric projection
  const float isoFov = 15.0f * kDeg2Rad, isoPitch = 38.0f * kDeg2Rad;
  const float tdPitchBase = lerp(kTdPitch, isoPitch, isoBlend_);
  const float tdFovBase = lerp(kTdFov, isoFov, isoBlend_);
  const float lensK = std::tan(kTdFov * 0.5f) / std::tan(tdFovBase * 0.5f);
  float tdDist = tdDist_ * lerp(1.0f, lensK, isoBlend_) * (in.indoors ? 0.62f : 1.0f) * (1.0f + tdSpeedZoom_);
  // look over buildings: if a building between the camera and the focus would hide it, raise the pitch
  if (t < 0.99f && !in.indoors) {
    Vec3 back = -forwardFromYaw(lerpAngle(tdYaw_, tpYaw_, t));
    float tanP = std::tan(tdPitchBase + tdPitchLift_);
    float need = 0;
    for (float h : {3.0f, 6.0f, 9.0f, 13.0f}) {
      float d = phys::raycast(w, {focus_.x, focus_.z}, {back.x, back.z}, tdDist, h);
      if (d < tdDist && d * std::tan(tdPitchBase) < h + 0.5f) need = std::max(need, std::atan2(h + 1.0f, std::max(d, 0.5f)) - tdPitchBase);
    }
    (void)tanP;
    need = clamp(need, 0.0f, 16.0f * kDeg2Rad);
    tdPitchLift_ += (need - tdPitchLift_) * expDecay(need > tdPitchLift_ ? 5.0f : 1.2f, dt);
    if (first_) tdPitchLift_ = need;
  }
  float tpDist = tpDist_ * (in.driving ? 1.55f : 1.0f);
  if (in.driving) tpDist *= 1.0f + clamp(std::fabs(in.speed) / 45.0f, 0.0f, 0.5f) * 0.5f;
  // third-person camera collision: pull the camera in front of walls and buildings
  {
    Vec3 dirH = Vec3{-std::sin(tpYaw_), 0, std::cos(tpYaw_)};  // from the focus back to the camera (horizontal)
    float cp = std::cos(tpPitch_), sp = std::sin(tpPitch_);
    float horiz = tpDist * cp;
    float eyeY = focus_.y + tpDist * sp;
    float hit = phys::raycast(w, {focus_.x, focus_.z}, {dirH.x, dirH.z}, horiz + 0.3f, eyeY);
    float want2 = hit < horiz + 0.3f ? std::max(0.9f * cp, hit - 0.35f) : horiz;
    float scale = clamp(want2 / std::max(horiz, 0.01f), 0.25f, 1.0f);
    // smooth: close quickly, open slowly
    float rate = scale < tpHitDist_ ? 18.0f : 3.0f;
    tpHitDist_ += (scale - tpHitDist_) * expDecay(rate, dt);
    if (first_) tpHitDist_ = scale;
    tpDist *= std::min(1.0f, tpHitDist_);
  }
  float yaw = lerpAngle(tdYaw_, tpYaw_, t);
  if (blendEased_ > 0.0f && blendEased_ < 1.0f) {
    // sweep the shorter way
    yaw = tdYaw_ + angleDiff(tdYaw_, tpYaw_) * t;
  }
  pitch_ = lerp(tdPitchBase + tdPitchLift_, tpPitch_, t);
  dist_ = std::exp(lerp(std::log(tdDist), std::log(tpDist), t));
  fov_ = lerp(tdFovBase, kTpFov + (in.driving ? clamp(in.speed / 40.0f, 0.0f, 1.0f) * 6.0f * kDeg2Rad : 0.0f), t);
  yaw_ = yaw;

  Vec3 fwdH = forwardFromYaw(yaw_);
  float cp = std::cos(pitch_), sp = std::sin(pitch_);
  eye_ = focus_ - fwdH * (dist_ * cp) + Vec3{0, dist_ * sp, 0};
  if (in.shake > 0.0f) {
    float s = in.shake * 0.08f;
    eye_ += Vec3{std::sin(dist_ * 91.7f + yaw_ * 13.0f) * s, std::sin(dist_ * 53.1f) * s, std::cos(dist_ * 77.3f) * s};
  }
  // keep the eye above the ground
  eye_.y = std::max(eye_.y, 0.45f);
  first_ = false;

  view_ = Mat4::lookAt(eye_, focus_, {0, 1, 0});
  float zn = lerp(1.0f, 0.2f, t), zf = 520.0f;
  nearZ_ = zn; farZ_ = zf;
  proj_ = Mat4::perspective(fov_, aspect, zn, zf);
  viewProj_ = proj_ * view_;
  frustum_.fromViewProj(viewProj_);
  right_ = {view_.at(0, 0), view_.at(0, 1), view_.at(0, 2)};
  up_ = {view_.at(1, 0), view_.at(1, 1), view_.at(1, 2)};
  fwd_ = {-view_.at(2, 0), -view_.at(2, 1), -view_.at(2, 2)};
}

}  // namespace gtabr
