// Visual identity of the interface: mature, quiet and dark. Graphite glass, thin hairlines, one cool silver accent; colour is
// reserved for meaning (danger, success). Nothing here is borrowed from any other game: no copied layouts, fonts or icons.
#pragma once
#include "ui.h"

namespace gtabr {
namespace theme {
inline const Color kInk = rgba(0.93f, 0.94f, 0.96f);        // primary text
inline const Color kDim = rgba(0.62f, 0.65f, 0.70f);        // secondary text
inline const Color kDimHi = rgba(0.84f, 0.86f, 0.90f);      // secondary text in high-contrast mode
inline const Color kFaint = rgba(0.62f, 0.65f, 0.70f, 0.40f);
inline const Color kLine = rgba(1.0f, 1.0f, 1.0f, 0.11f);   // hairlines / borders
inline const Color kLineHi = rgba(1.0f, 1.0f, 1.0f, 0.34f);
inline const Color kGlass = rgba(0.030f, 0.034f, 0.042f, 0.58f);
inline const Color kGlassHi = rgba(0.030f, 0.034f, 0.042f, 0.90f);
inline const Color kPanel = rgba(0.026f, 0.030f, 0.038f, 0.84f);
inline const Color kPanelHc = rgba(0.0f, 0.0f, 0.0f, 0.97f);
inline const Color kRow = rgba(1.0f, 1.0f, 1.0f, 0.045f);
inline const Color kRowHot = rgba(1.0f, 1.0f, 1.0f, 0.13f);
inline const Color kAcc = rgba(0.80f, 0.86f, 0.93f);        // silver-blue accent
inline const Color kOk = rgba(0.56f, 0.80f, 0.68f);
inline const Color kWarn = rgba(0.93f, 0.72f, 0.40f);
inline const Color kHot = rgba(0.90f, 0.40f, 0.38f);
}  // namespace theme
}  // namespace gtabr
