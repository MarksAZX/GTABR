#include "input.h"

#include <algorithm>
#include <cmath>

namespace gtabr {

void InputSystem::reset() {
  touches_.clear();
  ended_.clear();
  joyId_ = -1;
  wheelId_ = -1;
  pendingLook_ = {};
  pendingZoom_ = 0;
  for (int i = 0; i < kBtn; ++i) pressedBtn_[i] = heldBtn_[i] = false;
}

void InputSystem::onTouch(int id, TouchAction a, float x, float y) {
  Vec2 p{x, y};
  auto it = std::find_if(touches_.begin(), touches_.end(), [&](const Touch& t) { return t.id == id; });
  if (a == TouchAction::Down) {
    if (it != touches_.end()) return;
    Touch t;
    t.id = id; t.pos = t.start = t.prev = p;
    // role assignment uses the layout from the previous frame
    const InputLayout& L = lastLayout_;
    if (L.modal) t.role = Role::Ui;
    else {
      const HudButton* btns[kBtn] = {&L.run, &L.interact, &L.enterExit, &L.camera, &L.wheel, &L.pause, &L.attack, &L.reload, &L.jump};
      for (int i = 0; i < kBtn; ++i) {
        const HudButton& b = *btns[i];
        if (!b.visible) continue;
        float dx = p.x - b.c.x, dy = p.y - b.c.y;
        float rr = b.r * 1.18f;
        if (dx * dx + dy * dy <= rr * rr) {
          t.role = i == 4 ? Role::Wheel : Role::Button;
          t.button = i;
          if (i == 4) { wheelPressed_ = true; wheelId_ = id; }
          else { pressedBtn_[i] = true; heldBtn_[i] = true; }
          break;
        }
      }
      if (t.role == Role::None) {
        if (p.x < L.joyZoneRight && joyId_ < 0) {
          t.role = Role::Joystick;
          joyId_ = id;
          joyBase_ = p;
          joyKnob_ = p;
          // keep the base fully visible
          joyBase_.x = clamp(joyBase_.x, L.joyRadius * 0.9f, L.joyZoneRight);
          joyBase_.y = clamp(joyBase_.y, L.height * 0.35f, L.height - L.joyRadius * 0.9f);
          joyKnob_ = p;
        } else if (p.x >= L.joyZoneRight) t.role = Role::Look;
      }
    }
    touches_.push_back(t);
  } else if (it != touches_.end()) {
    if (a == TouchAction::Move) {
      Vec2 d = p - it->pos;
      it->prev = it->pos;
      it->pos = p;
      if (it->role == Role::Look) it->accum += d;
    } else {  // Up / Cancel
      it->pos = p;
      it->released = true;
      it->down = false;
      if (it->role == Role::Joystick) joyId_ = -1;
      if (it->role == Role::Button && it->button >= 0) heldBtn_[it->button] = false;
      if (it->role == Role::Wheel) { wheelReleased_ = true; wheelId_ = -1; }
      ended_.push_back(*it);
      touches_.erase(it);
    }
  }
}

InputFrame InputSystem::poll(const InputLayout& layout) {
  lastLayout_ = layout;
  InputFrame f;
  // joystick
  for (const Touch& t : touches_)
    if (t.role == Role::Joystick) {
      Vec2 d = t.pos - joyBase_;
      float maxR = layout.joyRadius;
      float len = d.length();
      Vec2 k = len > maxR ? d * (maxR / len) : d;
      joyKnob_ = joyBase_ + k;
      Vec2 n = k / maxR;
      float mag = n.length();
      const float dead = 0.12f;
      if (mag > dead) {
        float m = (mag - dead) / (1.0f - dead);
        n = n.normalized() * std::min(1.0f, m);
        f.move = {n.x, -n.y};
      }
      f.joyActive = true;
      f.joyBase = joyBase_;
      f.joyKnob = joyKnob_;
    }
  // look / pinch
  std::vector<Touch*> looks;
  for (Touch& t : touches_)
    if (t.role == Role::Look) looks.push_back(&t);
  if (looks.size() >= 2) {
    float d = (looks[0]->pos - looks[1]->pos).length();
    if (lastPinchDist_ > 0) f.zoom = d - lastPinchDist_;
    lastPinchDist_ = d;
    looks[0]->accum = {}; looks[1]->accum = {};
  } else {
    lastPinchDist_ = 0;
    if (looks.size() == 1) { f.look = looks[0]->accum; looks[0]->accum = {}; f.lookDragging = true; }
  }
  // buttons
  f.interactPressed = pressedBtn_[1]; f.enterExitPressed = pressedBtn_[2]; f.cameraPressed = pressedBtn_[3]; f.pausePressed = pressedBtn_[5];
  f.runHeld = heldBtn_[0];
  f.attackPressed = pressedBtn_[6]; f.attackHeld = heldBtn_[6]; f.reloadPressed = pressedBtn_[7]; f.jumpPressed = pressedBtn_[8]; f.jumpHeld = heldBtn_[8];
  f.interactHeld = heldBtn_[1]; f.enterExitHeld = heldBtn_[2]; f.cameraHeld = heldBtn_[3]; f.pauseHeld = heldBtn_[5];
  f.wheelPressed = wheelPressed_;
  f.wheelReleased = wheelReleased_;
  f.wheelHeld = wheelId_ >= 0;
  f.wheelBtnHeld = f.wheelHeld;
  for (const Touch& t : touches_) if (t.role == Role::Wheel) f.wheelPos = t.pos;
  for (const Touch& t : ended_) if (t.role == Role::Wheel) f.wheelPos = t.pos;
  // UI pointers (menus)
  auto uiFrom = [&](const Touch& t, bool pressed, bool released) {
    UiPointer p;
    p.id = t.id; p.pos = t.pos; p.start = t.start; p.down = t.down; p.pressed = pressed; p.released = released;
    f.ui.push_back(p);
  };
  for (Touch& t : touches_) {
    if (t.role == Role::Ui || layout.modal) {
      uiFrom(t, t.pressed, false);
      t.pressed = false;
    }
  }
  for (const Touch& t : ended_)
    if (t.role == Role::Ui || layout.modal) uiFrom(t, false, true);
  // clear edges
  for (int i = 0; i < kBtn; ++i) pressedBtn_[i] = false;
  wheelPressed_ = false;
  wheelReleased_ = false;
  ended_.clear();
  for (Touch& t : touches_) t.pressed = false;
  return f;
}

}  // namespace gtabr
