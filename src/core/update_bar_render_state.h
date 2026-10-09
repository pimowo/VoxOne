#ifndef VOXONE_UPDATE_BAR_RENDER_STATE_H
#define VOXONE_UPDATE_BAR_RENDER_STATE_H

#include "update_display_view.h"

struct UpdateBarRenderDelta {
  UpdateBarRenderDelta(bool resetValue = false, uint16_t clearXValue = 0,
                       uint16_t clearWidthValue = 0, uint16_t fillXValue = 0,
                       uint16_t fillWidthValue = 0)
      : reset(resetValue), clearX(clearXValue), clearWidth(clearWidthValue),
        fillX(fillXValue), fillWidth(fillWidthValue) {}
  bool reset;
  uint16_t clearX;
  uint16_t clearWidth;
  uint16_t fillX;
  uint16_t fillWidth;
};

// Coordinates are relative to the 356-pixel interior of the A0 update bar.
class UpdateBarRenderState {
public:
  void reset() { initialized_ = false; }

  UpdateBarRenderDelta apply(UpdateDisplayProgress progress) {
    const uint16_t width = progress.determinate
        ? static_cast<uint16_t>(356u * (progress.percent > 100 ? 100 : progress.percent) / 100u)
        : 56;
    if (!initialized_ || progress.determinate != determinate_) {
      initialized_ = true;
      determinate_ = progress.determinate;
      filledWidth_ = progress.determinate ? width : 0;
      segmentX_ = 0;
      return {true, 0, 0, 0, width};
    }
    if (!determinate_) return {};
    const uint16_t previous = filledWidth_;
    filledWidth_ = width;
    if (width > previous) return {false, 0, 0, previous,
                                  static_cast<uint16_t>(width - previous)};
    if (width < previous) return {false, width,
                                  static_cast<uint16_t>(previous - width), 0, 0};
    return {};
  }

  UpdateBarRenderDelta step() {
    if (!initialized_ || determinate_) return {};
    const uint16_t previous = segmentX_;
    segmentX_ = static_cast<uint16_t>((segmentX_ + 12u) % 300u);
    return {false, previous, 56, segmentX_, 56};
  }

private:
  bool initialized_ = false;
  bool determinate_ = false;
  uint16_t filledWidth_ = 0;
  uint16_t segmentX_ = 0;
};

#endif
