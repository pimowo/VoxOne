#ifndef VOXONE_DISPLAY_SCROLL_ENGINE_H
#define VOXONE_DISPLAY_SCROLL_ENGINE_H

#include <stdint.h>

#include "display_scroll_model.h"

namespace voxone {
namespace display_scroll {

enum class ScrollEnginePhase : uint8_t {
  Idle = 0,
  Waiting = 1,
  Moving = 2
};

enum class ScrollEngineAction : uint8_t {
  None = 0,
  TurnStarted = 1,
  Step = 2
};

// The renderer owns the text and its measurement. contentRevision must change
// whenever that row's text changes, even if its measured width stays the same.
// cyclePixels is the complete renderer-defined scroll cycle; the engine does
// not prescribe separators, duplication or rasterization.
struct ScrollRowDescriptor {
  uint8_t present;
  uint8_t needsScroll;
  uint16_t cyclePixels;
  uint32_t contentRevision;
};

struct ScrollEngineEvent {
  ScrollEngineAction action;
  ScrollEnginePhase phase;
  int8_t row;
  uint8_t stepPixels;
  uint16_t offsetPixels;
  uint8_t turnFinished;
};

class ScrollGroupEngine {
 public:
  static constexpr uint8_t kMaximumRows = 3;
  static constexpr int8_t kNoRow = -1;

  ScrollGroupEngine()
      : parameters_{0, 0, 0, false, false},
        rows_{},
        phaseStartedMs_(0),
        lastStepMs_(0),
        rowCount_(0),
        nextRow_(0),
        activeRow_(kNoRow),
        phase_(ScrollEnginePhase::Idle) {}

  void configure(ScrollRendererProfile profile,
                 const ScrollSettings& settings,
                 ScrollSettingsSource source) {
    parameters_ = resolveScrollParameters(profile, settings, source);
    resetSchedule(true);
  }

  // Applies at most kMaximumRows descriptors. A false result reports an
  // invalid pointer/count while leaving the engine in a deterministic,
  // safely bounded state.
  bool setRows(const ScrollRowDescriptor* descriptors, uint8_t count) {
    const bool validPointer = count == 0 || descriptors != 0;
    const bool validCount = count <= kMaximumRows;
    const uint8_t boundedCount =
        validPointer ? (validCount ? count : kMaximumRows) : 0;
    const int8_t previousActive = activeRow_;
    bool activeChanged = false;

    for (uint8_t row = 0; row < kMaximumRows; ++row) {
      const ScrollRowDescriptor next =
          row < boundedCount ? descriptors[row] : emptyDescriptor();
      if (sameDescriptor(rows_[row].descriptor, next)) continue;
      rows_[row].descriptor = next;
      rows_[row].offsetPixels = 0;
      if (previousActive == static_cast<int8_t>(row)) activeChanged = true;
    }

    rowCount_ = boundedCount;
    if (rowCount_ == 0) {
      resetSchedule(false);
      return validPointer && validCount;
    }

    if (previousActive >= static_cast<int8_t>(rowCount_) || activeChanged) {
      abandonTurn(previousActive);
    } else if (activeRow_ >= 0 &&
               !qualifies(static_cast<uint8_t>(activeRow_))) {
      abandonTurn(activeRow_);
    } else if (nextRow_ >= rowCount_) {
      nextRow_ = 0;
    }
    return validPointer && validCount;
  }

  ScrollEngineEvent tick(uint32_t nowMs) {
    if (!parameters_.supported || !parameters_.enabled || rowCount_ == 0) {
      if (activeRow_ >= 0) resetSchedule(true);
      return noEvent();
    }

    if (activeRow_ >= 0 &&
        !qualifies(static_cast<uint8_t>(activeRow_))) {
      abandonTurn(activeRow_);
    }

    if (activeRow_ < 0) return startNextTurn(nowMs);

    if (phase_ == ScrollEnginePhase::Waiting) {
      if (!elapsedAtLeast(nowMs, phaseStartedMs_, parameters_.pauseMs))
        return noEvent();
      phase_ = ScrollEnginePhase::Moving;
      lastStepMs_ = nowMs;
      return advanceOneStep();
    }

    if (phase_ != ScrollEnginePhase::Moving ||
        !elapsedAtLeast(nowMs, lastStepMs_, parameters_.intervalMs))
      return noEvent();

    lastStepMs_ = nowMs;
    return advanceOneStep();
  }

  ScrollEnginePhase phase() const { return phase_; }
  int8_t activeRow() const { return activeRow_; }
  uint8_t rowCount() const { return rowCount_; }
  uint16_t offsetPixels(uint8_t row) const {
    return row < rowCount_ ? rows_[row].offsetPixels : 0;
  }
  const ResolvedScrollParameters& parameters() const { return parameters_; }

 private:
  struct RowState {
    ScrollRowDescriptor descriptor;
    uint16_t offsetPixels;
  };

  static constexpr ScrollRowDescriptor emptyDescriptor() {
    return ScrollRowDescriptor{0, 0, 0, 0};
  }

  static bool sameDescriptor(const ScrollRowDescriptor& left,
                             const ScrollRowDescriptor& right) {
    return left.present == right.present &&
           left.needsScroll == right.needsScroll &&
           left.cyclePixels == right.cyclePixels &&
           left.contentRevision == right.contentRevision;
  }

  static bool elapsedAtLeast(uint32_t nowMs, uint32_t startedMs,
                             uint16_t durationMs) {
    return static_cast<uint32_t>(nowMs - startedMs) >= durationMs;
  }

  bool qualifies(uint8_t row) const {
    const ScrollRowDescriptor& descriptor = rows_[row].descriptor;
    return descriptor.present != 0 && descriptor.needsScroll != 0 &&
           descriptor.cyclePixels != 0;
  }

  ScrollEngineEvent noEvent() const {
    return ScrollEngineEvent{ScrollEngineAction::None, phase_, activeRow_, 0,
                             activeRow_ >= 0
                                 ? rows_[static_cast<uint8_t>(activeRow_)]
                                       .offsetPixels
                                 : static_cast<uint16_t>(0),
                             0};
  }

  ScrollEngineEvent startNextTurn(uint32_t nowMs) {
    for (uint8_t inspected = 0; inspected < rowCount_; ++inspected) {
      const uint8_t row =
          static_cast<uint8_t>((nextRow_ + inspected) % rowCount_);
      if (!qualifies(row)) continue;

      activeRow_ = static_cast<int8_t>(row);
      rows_[row].offsetPixels = 0;
      phaseStartedMs_ = nowMs;
      lastStepMs_ = nowMs;
      phase_ = parameters_.pauseMs == 0 ? ScrollEnginePhase::Moving
                                        : ScrollEnginePhase::Waiting;
      return ScrollEngineEvent{ScrollEngineAction::TurnStarted, phase_,
                               activeRow_, 0, 0, 0};
    }
    phase_ = ScrollEnginePhase::Idle;
    activeRow_ = kNoRow;
    return noEvent();
  }

  ScrollEngineEvent advanceOneStep() {
    const uint8_t row = static_cast<uint8_t>(activeRow_);
    RowState& state = rows_[row];
    const uint16_t remaining = static_cast<uint16_t>(
        state.descriptor.cyclePixels - state.offsetPixels);
    const uint8_t step = remaining < parameters_.stepPixels
                             ? static_cast<uint8_t>(remaining)
                             : parameters_.stepPixels;
    const uint16_t nextOffset =
        static_cast<uint16_t>(state.offsetPixels + step);
    if (nextOffset < state.descriptor.cyclePixels) {
      state.offsetPixels = nextOffset;
      return ScrollEngineEvent{ScrollEngineAction::Step, phase_, activeRow_,
                               step, state.offsetPixels, 0};
    }

    // A complete renderer-defined cycle is visually equivalent to offset 0.
    // Reset before yielding the turn so non-active rows always remain at their
    // left edge.
    state.offsetPixels = 0;
    const int8_t completedRow = activeRow_;
    advanceCursorAfter(row);
    activeRow_ = kNoRow;
    phase_ = ScrollEnginePhase::Idle;
    return ScrollEngineEvent{ScrollEngineAction::Step, phase_, completedRow,
                             step, 0, 1};
  }

  void abandonTurn(int8_t previousActive) {
    if (previousActive >= 0 &&
        previousActive < static_cast<int8_t>(kMaximumRows)) {
      rows_[static_cast<uint8_t>(previousActive)].offsetPixels = 0;
      if (rowCount_ != 0 && previousActive < static_cast<int8_t>(rowCount_))
        advanceCursorAfter(static_cast<uint8_t>(previousActive));
      else
        nextRow_ = 0;
    } else if (nextRow_ >= rowCount_) {
      nextRow_ = 0;
    }
    activeRow_ = kNoRow;
    phase_ = ScrollEnginePhase::Idle;
  }

  void advanceCursorAfter(uint8_t row) {
    nextRow_ = rowCount_ == 0
                   ? 0
                   : static_cast<uint8_t>((row + 1U) % rowCount_);
  }

  void resetSchedule(bool resetOffsets) {
    if (resetOffsets) {
      for (uint8_t row = 0; row < kMaximumRows; ++row)
        rows_[row].offsetPixels = 0;
    }
    phaseStartedMs_ = 0;
    lastStepMs_ = 0;
    nextRow_ = 0;
    activeRow_ = kNoRow;
    phase_ = ScrollEnginePhase::Idle;
  }

  ResolvedScrollParameters parameters_;
  RowState rows_[kMaximumRows];
  uint32_t phaseStartedMs_;
  uint32_t lastStepMs_;
  uint8_t rowCount_;
  uint8_t nextRow_;
  int8_t activeRow_;
  ScrollEnginePhase phase_;
};

}  // namespace display_scroll
}  // namespace voxone

#endif
