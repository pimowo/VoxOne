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

class UpdateBarRenderState {
public:
  UpdateBarRenderState(uint16_t width = 356, uint16_t segmentWidth = 56,
                       uint16_t step = 12)
      : width_(width), segmentWidth_(segmentWidth > width ? width : segmentWidth),
        step_(step), segmentTravel_(width_ - segmentWidth_) {}

  void reset() { initialized_ = false; }

  UpdateBarRenderDelta apply(UpdateDisplayProgress progress) {
    const uint16_t width = progress.determinate
        ? static_cast<uint16_t>(static_cast<uint32_t>(width_) *
                                (progress.percent > 100 ? 100 : progress.percent) / 100u)
        : segmentWidth_;
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
    segmentX_ = segmentTravel_ == 0
        ? 0 : static_cast<uint16_t>((segmentX_ + step_) % segmentTravel_);
    return {false, previous, segmentWidth_, segmentX_, segmentWidth_};
  }

private:
  uint16_t width_;
  uint16_t segmentWidth_;
  uint16_t step_;
  uint16_t segmentTravel_;
  bool initialized_ = false;
  bool determinate_ = false;
  uint16_t filledWidth_ = 0;
  uint16_t segmentX_ = 0;
};

#endif
