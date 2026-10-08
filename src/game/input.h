// Multi-touch input: floating joystick on the left, camera drag / pinch on the right, on-screen buttons.
#pragma once
#include <vector>

#include "../core/math.h"

namespace gtabr {

enum class TouchAction { Down, Move, Up, Cancel };

struct HudButton {
  Vec2 c;
  float r = 0;
  bool visible = true;
};

// Layout computed by the HUD every frame (pixels).
struct InputLayout {
  float width = 1920, height = 1080;
  HudButton run, interact, enterExit, camera, wheel, pause, attack, reload, jump;
  float joyZoneRight = 0;       // touches left of this x start the joystick
  float joyRadius = 120;
  bool modal = false;           // a menu/panel is open: touches go to the UI instead of gameplay
};

struct UiPointer {
  int id = -1;
  Vec2 pos, start;
  bool down = false, pressed = false, released = false;
};

struct InputFrame {
  Vec2 move;                 // joystick, x right, y up (-1..1)
  bool joyActive = false;
  Vec2 joyBase, joyKnob;     // pixels (for drawing)
  Vec2 look;                 // accumulated drag this frame (pixels)
  float zoom = 0;            // pinch delta (pixels, + = fingers apart)
  bool lookDragging = false;
  bool runHeld = false;
  bool interactPressed = false, enterExitPressed = false, cameraPressed = false, pausePressed = false;
  bool wheelHeld = false, wheelPressed = false, wheelReleased = false;
  Vec2 wheelPos;
  bool interactHeld = false, enterExitHeld = false, cameraHeld = false, wheelBtnHeld = false, pauseHeld = false;
  bool attackPressed = false, attackHeld = false, reloadPressed = false, jumpPressed = false, jumpHeld = false;
  std::vector<UiPointer> ui;  // pointers for menus
};

class InputSystem {
 public:
  void onTouch(int id, TouchAction a, float x, float y);
  // Called once per frame with the current layout; produces the frame state and clears edges.
  InputFrame poll(const InputLayout& layout);
  void reset();
  bool anyTouch() const { return !touches_.empty(); }

 private:
  enum class Role { None, Joystick, Look, Button, Wheel, Ui };
  struct Touch {
    int id; Vec2 pos, start, prev; Role role = Role::None; int button = -1; bool down = true, released = false, pressed = true;
    Vec2 accum;
  };
  std::vector<Touch> touches_;
  std::vector<Touch> ended_;
  Vec2 pendingLook_;
  float pendingZoom_ = 0;
  Vec2 joyBase_, joyKnob_;
  int joyId_ = -1;
  static constexpr int kBtn = 9;
  bool pressedBtn_[kBtn] = {}, heldBtn_[kBtn] = {};
  bool wheelReleased_ = false, wheelPressed_ = false;
  int wheelId_ = -1;
  InputLayout lastLayout_;
  std::vector<Touch> lookTouches() const;
  float lastPinchDist_ = 0;
  bool modalLatch_ = false;
};

}  // namespace gtabr
