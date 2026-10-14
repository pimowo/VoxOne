#ifndef VOXONE_PLAYER_SCROLL_RUNTIME_H
#define VOXONE_PLAYER_SCROLL_RUNTIME_H

#include <stdint.h>

#include "player_scroll_controller.h"

#ifndef VOXONE_PLAYER_SCROLL_CORE_ENABLED
#define VOXONE_PLAYER_SCROLL_CORE_ENABLED 0
#endif

static_assert(VOXONE_PLAYER_SCROLL_CORE_ENABLED == 0 ||
                  VOXONE_PLAYER_SCROLL_CORE_ENABLED == 1,
              "VOXONE_PLAYER_SCROLL_CORE_ENABLED must be 0 or 1");

namespace voxone {
namespace display_scroll {

constexpr bool playerScrollCoreBuildEnabled() {
  return VOXONE_PLAYER_SCROLL_CORE_ENABLED != 0;
}

// Fixed binding to a renderer-owned widget. The runtime never owns or copies
// displayed text; callbacks expose cached metrics and absolute-offset drawing.
struct PlayerScrollWidgetBinding {
  void* context;
  PlayerScrollRowMetrics (*metrics)(const void* context);
  void (*setCoreControlled)(void* context, bool enabled);
  bool (*renderOffset)(void* context, uint16_t offsetPixels);
};

struct PlayerScrollRuntimeTick {
  PlayerScrollCommand command;
  uint8_t renderRequested;
  uint8_t renderAccepted;
};

// Display-owned bridge between one PlayerScrollController and the widgets in
// the active PLAYER layout. It performs no allocation, timing or rendering of
// its own and emits at most one widget render request per tick.
class PlayerScrollRuntime {
 public:
  static constexpr uint8_t kMaximumRows = PlayerScrollController::kMaximumRows;

  PlayerScrollRuntime()
      : controller_{}, bindings_{}, rowCount_(0), enabled_(false), active_(false) {}

  bool configure(bool enabled, ScrollRendererProfile profile,
                 const ScrollSettings& settings, ScrollSettingsSource source) {
    if (enabled_ == enabled &&
        controller_.configurationMatches(profile, settings, source))
      return false;
    if (active_) leave();
    enabled_ = enabled;
    (void)controller_.configure(profile, settings, source);
    return true;
  }

  bool setRows(const PlayerScrollWidgetBinding* bindings, uint8_t count) {
    const bool validPointer = count == 0 || bindings != 0;
    const bool validCount = count <= kMaximumRows;
    if (!validPointer || !validCount) {
      if (active_) leave();
      clearBindings();
      return false;
    }

    if (active_) leave();
    clearBindings();
    for (uint8_t row = 0; row < count; ++row) bindings_[row] = bindings[row];
    rowCount_ = count;
    return bindingsValid();
  }

  void enter(uint32_t nowMs) {
    if (!enabled_ || rowCount_ == 0 || !bindingsValid()) return;
    for (uint8_t row = 0; row < rowCount_; ++row)
      bindings_[row].setCoreControlled(bindings_[row].context, true);
    active_ = true;
    refreshMetrics();
    controller_.enter(nowMs);
  }

  void leave() {
    controller_.leave();
    if (active_) {
      for (uint8_t row = 0; row < rowCount_; ++row)
        bindings_[row].setCoreControlled(bindings_[row].context, false);
    }
    active_ = false;
  }

  PlayerScrollRuntimeTick tick(uint32_t nowMs) {
    if (!active_) return noTick();
    refreshMetrics();
    const PlayerScrollCommand command = controller_.tick(nowMs);
    if (command.action != PlayerScrollCommandAction::RenderOffset ||
        command.row < 0 || command.row >= static_cast<int8_t>(rowCount_))
      return PlayerScrollRuntimeTick{command, 0, 0};

    const PlayerScrollWidgetBinding& binding =
        bindings_[static_cast<uint8_t>(command.row)];
    const bool accepted = binding.renderOffset(binding.context,
                                                command.offsetPixels);
    return PlayerScrollRuntimeTick{command, 1,
                                   static_cast<uint8_t>(accepted)};
  }

  bool enabled() const { return enabled_; }
  bool active() const { return active_; }
  uint8_t rowCount() const { return rowCount_; }
  const PlayerScrollController& controller() const { return controller_; }

 private:
  static constexpr PlayerScrollWidgetBinding emptyBinding() {
    return PlayerScrollWidgetBinding{0, 0, 0, 0};
  }

  static PlayerScrollRuntimeTick noTick() {
    return PlayerScrollRuntimeTick{
        PlayerScrollCommand{PlayerScrollCommandAction::None,
                            ScrollEnginePhase::Idle,
                            ScrollGroupEngine::kNoRow, 0, 0},
        0, 0};
  }

  void clearBindings() {
    for (uint8_t row = 0; row < kMaximumRows; ++row)
      bindings_[row] = emptyBinding();
    rowCount_ = 0;
  }

  bool bindingsValid() const {
    for (uint8_t row = 0; row < rowCount_; ++row) {
      const PlayerScrollWidgetBinding& binding = bindings_[row];
      if (!binding.context || !binding.metrics || !binding.setCoreControlled ||
          !binding.renderOffset)
        return false;
    }
    return true;
  }

  void refreshMetrics() {
    PlayerScrollRowMetrics metrics[kMaximumRows]{};
    for (uint8_t row = 0; row < rowCount_; ++row)
      metrics[row] = bindings_[row].metrics(bindings_[row].context);
    (void)controller_.setRows(metrics, rowCount_);
  }

  PlayerScrollController controller_;
  PlayerScrollWidgetBinding bindings_[kMaximumRows];
  uint8_t rowCount_;
  bool enabled_;
  bool active_;
};

}  // namespace display_scroll
}  // namespace voxone

#endif
