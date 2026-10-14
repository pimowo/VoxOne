#ifndef VOXONE_PLAYER_SCROLL_CONTROLLER_H
#define VOXONE_PLAYER_SCROLL_CONTROLLER_H

#include <stdint.h>

#include "display_scroll_engine.h"

namespace voxone {
namespace display_scroll {

// Metrics describe renderer-owned text without exposing or copying it. The
// layout supplies rows in its logical order (Station, Artist, Title today).
struct PlayerScrollRowMetrics {
  uint8_t present;
  uint16_t viewportPixels;
  uint16_t textPixels;
  uint16_t cyclePixels;
  uint32_t contentRevision;
};

enum class PlayerScrollCommandAction : uint8_t {
  None = 0,
  RenderOffset = 1
};

struct PlayerScrollCommand {
  PlayerScrollCommandAction action;
  ScrollEnginePhase phase;
  int8_t row;
  uint16_t offsetPixels;
  uint8_t turnFinished;
};

// Fixed-capacity bridge between renderer measurements and ScrollGroupEngine.
// It owns scheduling and offsets, but never owns text or performs rendering.
class PlayerScrollController {
 public:
  static constexpr uint8_t kMaximumRows = ScrollGroupEngine::kMaximumRows;

  PlayerScrollController()
      : engine_{},
        metrics_{},
        descriptors_{},
        descriptorRevisions_{},
        profile_(ScrollRendererProfile::None),
        settings_(defaultScrollSettings()),
        source_(ScrollSettingsSource::RendererDefaults),
        rowCount_(0),
        pendingResetMask_(0),
        active_(false) {}

  void configure(ScrollRendererProfile profile,
                 const ScrollSettings& settings,
                 ScrollSettingsSource source) {
    profile_ = profile;
    settings_ = settings;
    source_ = source;
    rebuildEngine();
    if (active_) pendingResetMask_ = presentRowsMask();
  }

  // Invalid input is rejected and converted to a safe empty layout.
  bool setRows(const PlayerScrollRowMetrics* rows, uint8_t count) {
    const bool validPointer = count == 0 || rows != 0;
    const bool validCount = count <= kMaximumRows;
    const uint8_t boundedCount =
        validPointer && validCount ? count : static_cast<uint8_t>(0);
    uint8_t changedMask = 0;

    for (uint8_t row = 0; row < kMaximumRows; ++row) {
      const PlayerScrollRowMetrics next =
          row < boundedCount ? normalizeMetrics(rows[row]) : emptyMetrics();
      if (!sameMetrics(metrics_[row], next)) {
        metrics_[row] = next;
        descriptorRevisions_[row] = nextDescriptorRevision(
            descriptorRevisions_[row]);
        changedMask = static_cast<uint8_t>(changedMask | (1U << row));
      }
      descriptors_[row] = descriptorFor(metrics_[row],
                                        descriptorRevisions_[row]);
    }

    rowCount_ = boundedCount;
    (void)engine_.setRows(descriptors_, rowCount_);
    if (active_)
      pendingResetMask_ = static_cast<uint8_t>(
          pendingResetMask_ | (changedMask & presentRowsMask()));
    return validPointer && validCount;
  }

  // Starts a fresh PLAYER cycle. The first pause is anchored at enter(), not
  // at an arbitrary later renderer callback.
  void enter(uint32_t nowMs) {
    active_ = true;
    rebuildEngine();
    pendingResetMask_ = presentRowsMask();
    (void)engine_.tick(nowMs);
  }

  void leave() {
    active_ = false;
    pendingResetMask_ = 0;
    rebuildEngine();
  }

  PlayerScrollCommand tick(uint32_t nowMs) {
    if (!active_) return noCommand();

    if (pendingResetMask_ != 0) {
      const uint8_t row = firstPendingReset();
      pendingResetMask_ = static_cast<uint8_t>(
          pendingResetMask_ & ~(static_cast<uint8_t>(1U << row)));
      return PlayerScrollCommand{PlayerScrollCommandAction::RenderOffset,
                                 engine_.phase(), static_cast<int8_t>(row),
                                 0, 0};
    }

    const ScrollEngineEvent event = engine_.tick(nowMs);
    if (event.action != ScrollEngineAction::Step)
      return PlayerScrollCommand{PlayerScrollCommandAction::None, event.phase,
                                 event.row, event.offsetPixels,
                                 event.turnFinished};
    return PlayerScrollCommand{PlayerScrollCommandAction::RenderOffset,
                               event.phase, event.row, event.offsetPixels,
                               event.turnFinished};
  }

  bool active() const { return active_; }
  uint8_t rowCount() const { return rowCount_; }
  ScrollEnginePhase phase() const {
    return active_ ? engine_.phase() : ScrollEnginePhase::Idle;
  }
  int8_t activeRow() const {
    return active_ ? engine_.activeRow() : ScrollGroupEngine::kNoRow;
  }
  uint16_t offsetPixels(uint8_t row) const {
    return active_ ? engine_.offsetPixels(row) : 0;
  }
  const ResolvedScrollParameters& parameters() const {
    return engine_.parameters();
  }

 private:
  static constexpr PlayerScrollRowMetrics emptyMetrics() {
    return PlayerScrollRowMetrics{0, 0, 0, 0, 0};
  }

  static PlayerScrollRowMetrics normalizeMetrics(
      const PlayerScrollRowMetrics& metrics) {
    return PlayerScrollRowMetrics{
        static_cast<uint8_t>(metrics.present != 0), metrics.viewportPixels,
        metrics.textPixels, metrics.cyclePixels, metrics.contentRevision};
  }

  static bool sameMetrics(const PlayerScrollRowMetrics& left,
                          const PlayerScrollRowMetrics& right) {
    return left.present == right.present &&
           left.viewportPixels == right.viewportPixels &&
           left.textPixels == right.textPixels &&
           left.cyclePixels == right.cyclePixels &&
           left.contentRevision == right.contentRevision;
  }

  static uint32_t nextDescriptorRevision(uint32_t revision) {
    ++revision;
    return revision == 0 ? 1 : revision;
  }

  static ScrollRowDescriptor descriptorFor(
      const PlayerScrollRowMetrics& metrics, uint32_t revision) {
    const bool hasGeometry = metrics.viewportPixels != 0;
    const bool hasText = metrics.textPixels != 0;
    const bool needsScroll = metrics.present != 0 && hasGeometry && hasText &&
                             metrics.textPixels > metrics.viewportPixels &&
                             metrics.cyclePixels >= metrics.textPixels;
    return ScrollRowDescriptor{
        metrics.present, static_cast<uint8_t>(needsScroll),
        needsScroll ? metrics.cyclePixels : static_cast<uint16_t>(0), revision};
  }

  void rebuildEngine() {
    engine_.configure(profile_, settings_, source_);
    (void)engine_.setRows(descriptors_, rowCount_);
  }

  uint8_t presentRowsMask() const {
    uint8_t mask = 0;
    for (uint8_t row = 0; row < rowCount_; ++row) {
      if (metrics_[row].present)
        mask = static_cast<uint8_t>(mask | (1U << row));
    }
    return mask;
  }

  uint8_t firstPendingReset() const {
    for (uint8_t row = 0; row < kMaximumRows; ++row)
      if (pendingResetMask_ & (1U << row)) return row;
    return 0;
  }

  PlayerScrollCommand noCommand() const {
    return PlayerScrollCommand{PlayerScrollCommandAction::None,
                               ScrollEnginePhase::Idle,
                               ScrollGroupEngine::kNoRow, 0, 0};
  }

  ScrollGroupEngine engine_;
  PlayerScrollRowMetrics metrics_[kMaximumRows];
  ScrollRowDescriptor descriptors_[kMaximumRows];
  uint32_t descriptorRevisions_[kMaximumRows];
  ScrollRendererProfile profile_;
  ScrollSettings settings_;
  ScrollSettingsSource source_;
  uint8_t rowCount_;
  uint8_t pendingResetMask_;
  bool active_;
};

}  // namespace display_scroll
}  // namespace voxone

#endif
