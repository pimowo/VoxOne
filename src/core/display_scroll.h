#ifndef VOXONE_DISPLAY_SCROLL_H
#define VOXONE_DISPLAY_SCROLL_H

// Consolidated from display_scroll_model.h.
#include <stdint.h>

namespace voxone {
namespace display_scroll {

// User-facing values. Keep their numeric representation stable for the future
// persistence and wire adapters, but do not persist this in-memory structure.
enum class ScrollSpeed : uint8_t {
  Slow = 0,
  Normal = 1,
  Fast = 2
};

// These are presentation layouts, not hardware boards or controller drivers.
// SSD1306 and SSD1309 intentionally share Oled128x64.
enum class ScrollRendererProfile : uint8_t {
  None = 0,
  Oled128x64 = 1,
  St7789_284x76 = 2,
  St7796_480x320 = 3
};

// RendererDefaults preserves the existing renderer-specific behavior until a
// later integration explicitly activates the common user setting.
enum class ScrollSettingsSource : uint8_t {
  RendererDefaults = 0,
  UserSettings = 1
};

struct ScrollSettings {
  bool enabled;
  ScrollSpeed speed;
  // A future engine applies this delay before animation. Zero means no
  // intentional pause; it never means catch-up rendering.
  uint16_t pauseMs;
};

struct ScrollMotion {
  uint8_t stepPixels;
  uint16_t intervalMs;
};

struct ScrollRendererDefaults {
  bool supported;
  bool enabled;
  ScrollSpeed speed;
  uint16_t pauseMs;
};

struct ResolvedScrollParameters {
  uint16_t intervalMs;
  uint16_t pauseMs;
  uint8_t stepPixels;
  bool supported;
  bool enabled;
};

constexpr uint16_t kNominalDisplayLoopIntervalMs = 10;
constexpr uint16_t kMinimumPauseMs = 0;
constexpr uint16_t kMaximumPauseMs = 5000;
constexpr uint16_t kPauseStepMs = 1000;
constexpr bool kDefaultUserEnabled = true;
constexpr uint16_t kDefaultUserPauseMs = 5000;
constexpr ScrollSpeed kDefaultUserSpeed = ScrollSpeed::Normal;

constexpr ScrollSettings defaultScrollSettings() {
  return ScrollSettings{kDefaultUserEnabled, kDefaultUserSpeed,
                        kDefaultUserPauseMs};
}

// Raw adapters can validate future persisted or wire values before converting
// them to bool. The model itself deliberately does not define such an adapter.
constexpr bool isValidScrollEnabledValue(uint8_t enabled) {
  return enabled == 0 || enabled == 1;
}

constexpr bool normalizeScrollEnabled(uint8_t enabled) {
  return isValidScrollEnabledValue(enabled) ? enabled != 0
                                            : kDefaultUserEnabled;
}

constexpr bool isValidScrollSpeed(ScrollSpeed speed) {
  return speed == ScrollSpeed::Slow || speed == ScrollSpeed::Normal ||
         speed == ScrollSpeed::Fast;
}

constexpr bool isValidScrollPause(uint32_t pauseMs) {
  return pauseMs <= kMaximumPauseMs && pauseMs % kPauseStepMs == 0;
}

constexpr bool isValidScrollSettings(const ScrollSettings& settings) {
  return isValidScrollSpeed(settings.speed) &&
         isValidScrollPause(settings.pauseMs);
}

constexpr ScrollSpeed normalizeScrollSpeed(ScrollSpeed speed) {
  return isValidScrollSpeed(speed) ? speed : kDefaultUserSpeed;
}

constexpr uint16_t normalizeScrollPause(uint32_t pauseMs) {
  return isValidScrollPause(pauseMs) ? static_cast<uint16_t>(pauseMs)
                                     : kDefaultUserPauseMs;
}

constexpr ScrollSettings normalizeScrollSettings(
    const ScrollSettings& settings) {
  return ScrollSettings{settings.enabled, normalizeScrollSpeed(settings.speed),
                        normalizeScrollPause(settings.pauseMs)};
}

constexpr bool isKnownScrollRendererProfile(ScrollRendererProfile profile) {
  return profile == ScrollRendererProfile::None ||
         profile == ScrollRendererProfile::Oled128x64 ||
         profile == ScrollRendererProfile::St7789_284x76 ||
         profile == ScrollRendererProfile::St7796_480x320;
}

constexpr bool isValidScrollSettingsSource(ScrollSettingsSource source) {
  return source == ScrollSettingsSource::RendererDefaults ||
         source == ScrollSettingsSource::UserSettings;
}

constexpr ScrollRendererDefaults rendererScrollDefaults(
    ScrollRendererProfile profile) {
  return profile == ScrollRendererProfile::Oled128x64
             ? ScrollRendererDefaults{true, true, ScrollSpeed::Normal, 5000}
         : profile == ScrollRendererProfile::St7789_284x76
             ? ScrollRendererDefaults{true, true, ScrollSpeed::Normal, 5000}
         : profile == ScrollRendererProfile::St7796_480x320
             ? ScrollRendererDefaults{true, true, ScrollSpeed::Normal, 2500}
             : ScrollRendererDefaults{false, false, ScrollSpeed::Normal, 0};
}

constexpr ScrollMotion oled128x64ScrollMotion(ScrollSpeed speed) {
  return normalizeScrollSpeed(speed) == ScrollSpeed::Slow
             ? ScrollMotion{1, 35}
         : normalizeScrollSpeed(speed) == ScrollSpeed::Fast
             ? ScrollMotion{4, 35}
             : ScrollMotion{2, 35};
}

constexpr ScrollMotion st7789_284x76ScrollMotion(ScrollSpeed speed) {
  return normalizeScrollSpeed(speed) == ScrollSpeed::Slow
             ? ScrollMotion{1, 18}
         : normalizeScrollSpeed(speed) == ScrollSpeed::Fast
             ? ScrollMotion{2, 9}
             : ScrollMotion{1, 9};
}

constexpr ScrollMotion st7796_480x320ScrollMotion(ScrollSpeed speed) {
  return normalizeScrollSpeed(speed) == ScrollSpeed::Slow
             ? ScrollMotion{1, 50}
         : normalizeScrollSpeed(speed) == ScrollSpeed::Fast
             ? ScrollMotion{4, 50}
             : ScrollMotion{2, 50};
}

constexpr ScrollMotion scrollMotion(ScrollRendererProfile profile,
                                    ScrollSpeed speed) {
  return profile == ScrollRendererProfile::Oled128x64
             ? oled128x64ScrollMotion(speed)
         : profile == ScrollRendererProfile::St7789_284x76
             ? st7789_284x76ScrollMotion(speed)
         : profile == ScrollRendererProfile::St7796_480x320
             ? st7796_480x320ScrollMotion(speed)
             : ScrollMotion{0, 0};
}

// Row order and row state intentionally do not belong to this value model.
// A future fixed-capacity engine can select only the rows present in a layout,
// while sharing these settings without assuming a three-row display.

inline ResolvedScrollParameters resolveScrollParameters(
    ScrollRendererProfile profile, const ScrollSettings& userSettings,
    ScrollSettingsSource source) {
  const ScrollRendererDefaults defaults = rendererScrollDefaults(profile);
  if (!defaults.supported)
    return ResolvedScrollParameters{0, 0, 0, false, false};

  const bool useUserSettings =
      source == ScrollSettingsSource::UserSettings;
  const ScrollSettings normalized = normalizeScrollSettings(userSettings);
  const bool enabled = useUserSettings ? normalized.enabled : defaults.enabled;
  if (!enabled)
    return ResolvedScrollParameters{0, 0, 0, true, false};

  const ScrollSpeed speed = useUserSettings ? normalized.speed : defaults.speed;
  const uint16_t pauseMs =
      useUserSettings ? normalized.pauseMs : defaults.pauseMs;
  const ScrollMotion motion = scrollMotion(profile, speed);
  return ResolvedScrollParameters{motion.intervalMs, pauseMs,
                                  motion.stepPixels, true, true};
}

}  // namespace display_scroll
}  // namespace voxone

// Consolidated from display_scroll_engine.h.
#include <stdint.h>


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
        profile_(ScrollRendererProfile::None),
        rowCount_(0),
        nextRow_(0),
        activeRow_(kNoRow),
        phase_(ScrollEnginePhase::Idle),
        cycleBoundaryReached_(false) {}

  void configure(ScrollRendererProfile profile,
                 const ScrollSettings& settings,
                 ScrollSettingsSource source) {
    profile_ = profile;
    parameters_ = resolveScrollParameters(profile, settings, source);
    resetSchedule(true);
  }

  // The historical ST7796 scheduler gives the last changed PLAYER field the
  // next turn, even when another row was moving. The controller calls this
  // only for that renderer; other CORE renderers retain their fair-yield rule.
  void restartAtRow(uint8_t row) {
    if (rowCount_ == 0 || row >= rowCount_) return;
    nextRow_ = row;
    activeRow_ = kNoRow;
    phase_ = ScrollEnginePhase::Idle;
    cycleBoundaryReached_ = false;
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
      // ST7796 historically changes state at the end of its 2500 ms pause,
      // then waits one complete 50 ms cadence before moving two pixels.
      if (usesSt7796LegacyTiming()) return noEvent();
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
  bool usesSt7796LegacyTiming() const {
    return profile_ == ScrollRendererProfile::St7796_480x320;
  }

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
      cycleBoundaryReached_ = false;
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

    // ScrollWidget's legacy ST7796 path regards an offset exactly equal to a
    // cycle as the origin visually, but reports cycle completion only on the
    // following scheduled step. Preserve that one-cadence hold for parity.
    if (usesSt7796LegacyTiming() && cycleBoundaryReached_) {
      cycleBoundaryReached_ = false;
      return finishTurn(row, parameters_.stepPixels);
    }

    const uint16_t remaining = static_cast<uint16_t>(
        state.descriptor.cyclePixels - state.offsetPixels);
    const uint8_t step = usesSt7796LegacyTiming()
                             ? parameters_.stepPixels
                             : (remaining < parameters_.stepPixels
                                    ? static_cast<uint8_t>(remaining)
                                    : parameters_.stepPixels);
    const uint32_t nextOffset =
        static_cast<uint32_t>(state.offsetPixels) + step;
    if (nextOffset < state.descriptor.cyclePixels) {
      state.offsetPixels = static_cast<uint16_t>(nextOffset);
      return ScrollEngineEvent{ScrollEngineAction::Step, phase_, activeRow_,
                               step, state.offsetPixels, 0};
    }

    if (usesSt7796LegacyTiming() &&
        nextOffset == state.descriptor.cyclePixels) {
      state.offsetPixels = 0;
      cycleBoundaryReached_ = true;
      return ScrollEngineEvent{ScrollEngineAction::Step, phase_, activeRow_,
                               step, 0, 0};
    }

    return finishTurn(row, step);
  }

  ScrollEngineEvent finishTurn(uint8_t row, uint8_t step) {
    RowState& state = rows_[row];
    // A complete renderer-defined cycle is visually equivalent to offset 0.
    // Reset before yielding the turn so non-active rows always remain at their
    // left edge.
    state.offsetPixels = 0;
    const int8_t completedRow = activeRow_;
    advanceCursorAfter(row);
    activeRow_ = kNoRow;
    phase_ = ScrollEnginePhase::Idle;
    cycleBoundaryReached_ = false;
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
    cycleBoundaryReached_ = false;
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
    cycleBoundaryReached_ = false;
  }

  ResolvedScrollParameters parameters_;
  RowState rows_[kMaximumRows];
  uint32_t phaseStartedMs_;
  uint32_t lastStepMs_;
  ScrollRendererProfile profile_;
  uint8_t rowCount_;
  uint8_t nextRow_;
  int8_t activeRow_;
  ScrollEnginePhase phase_;
  bool cycleBoundaryReached_;
};

}  // namespace display_scroll
}  // namespace voxone

// Consolidated from display_scroll_persistence.h.
#include <stddef.h>
#include <stdint.h>


namespace voxone {
namespace display_scroll {

constexpr uint8_t kScrollSettingsRecordVersion = 1;
constexpr size_t kScrollSettingsRecordSize = 13;

enum class ScrollSettingsRecordStatus : uint8_t {
  Valid,
  NotDecoded,
  InvalidSize,
  InvalidMagic,
  InvalidVersion,
  InvalidCrc,
  InvalidEnabled,
  InvalidSpeed,
  InvalidPause
};

enum class ScrollSettingsStorageReadStatus : uint8_t {
  Found,
  Missing,
  OpenError,
  ReadError
};

enum class ScrollSettingsStorageWriteStatus : uint8_t {
  Stored,
  OpenError,
  WriteError,
  CommitError,
  VerifyError
};

enum class ScrollSettingsStorageEraseStatus : uint8_t {
  Erased,
  Missing,
  OpenError,
  EraseError,
  CommitError,
  VerifyError
};

enum class ScrollSettingsLoadStatus : uint8_t {
  UserSettings,
  RendererDefaultsMissing,
  RendererDefaultsInvalid,
  RendererDefaultsOpenError,
  RendererDefaultsReadError
};

enum class ScrollSettingsSaveStatus : uint8_t {
  Saved,
  Unchanged,
  Invalid,
  StorageOpenError,
  StorageWriteError,
  StorageCommitError,
  StorageVerifyError
};

enum class ScrollSettingsEraseStatus : uint8_t {
  Erased,
  Unchanged,
  StorageOpenError,
  StorageEraseError,
  StorageCommitError,
  StorageVerifyError
};

class ScrollSettingsStorage {
 public:
  virtual ~ScrollSettingsStorage() {}
  virtual ScrollSettingsStorageReadStatus read(uint8_t* output,
                                                size_t capacity,
                                                size_t& size) = 0;
  virtual ScrollSettingsStorageWriteStatus write(const uint8_t* input,
                                                   size_t size) = 0;
  virtual ScrollSettingsStorageEraseStatus erase() = 0;
};

// This is a value snapshot: it owns no storage handles and is safe to copy to
// a future DisplayTask handoff. RendererDefaults means that the renderer must
// resolve its historical values instead of interpreting settings as user data.
struct ScrollSettingsLoadResult {
  ScrollSettings settings;
  ScrollSettingsSource source;
  ScrollSettingsLoadStatus status;
  ScrollSettingsRecordStatus recordStatus;
};

bool equalScrollSettings(const ScrollSettings& left,
                         const ScrollSettings& right);
uint32_t scrollSettingsRecordCrc32(const uint8_t* data, size_t size);
bool serializeScrollSettingsRecord(const ScrollSettings& settings,
                                   uint8_t* output, size_t outputSize);
ScrollSettingsRecordStatus deserializeScrollSettingsRecord(
    const uint8_t* input, size_t inputSize, ScrollSettings& output);

ScrollSettingsLoadResult loadScrollSettings(ScrollSettingsStorage& storage);
ScrollSettingsSaveStatus saveScrollSettings(
    ScrollSettingsStorage& storage, const ScrollSettingsLoadResult& current,
    const ScrollSettings& requested);
ScrollSettingsEraseStatus eraseScrollSettings(ScrollSettingsStorage& storage);

// Production NVS adapter. Runtime code reads through the explicit boot/reload
// boundary in display_scroll_runtime_config; render ticks never call NVS.
ScrollSettingsLoadResult readScrollSettingsPersistent();
ScrollSettingsSaveStatus writeScrollSettingsPersistent(
    const ScrollSettingsLoadResult& current,
    const ScrollSettings& requested);
ScrollSettingsEraseStatus eraseScrollSettingsPersistent();

}  // namespace display_scroll
}  // namespace voxone

// Consolidated from display_scroll_runtime_config.h.
#include <atomic>
#include <stdint.h>


namespace voxone {
namespace display_scroll {

struct ScrollSettingsRuntimeSnapshot {
  ScrollSettings settings;
  ScrollSettingsSource source;
  uint32_t revision;
};

enum class ScrollSettingsPublishStatus : uint8_t {
  Changed,
  Unchanged,
  Invalid
};

// Single runtime owner for the complete scroll configuration. The compact
// atomic value prevents readers from observing settings and source from
// different publications and keeps DisplayTask independent from NVS.
class ScrollSettingsRuntimeConfig {
 public:
  ScrollSettingsRuntimeConfig();

  ScrollSettingsRuntimeSnapshot snapshot() const;
  ScrollSettingsPublishStatus publish(const ScrollSettings& settings,
                                      ScrollSettingsSource source);

 private:
  std::atomic<uint32_t> state_;
};

using ScrollSettingsPersistentReader = ScrollSettingsLoadResult (*)();

ScrollSettingsRuntimeConfig& scrollSettingsRuntimeConfig();

// Reads persistence exactly once and publishes either validated UserSettings
// or RendererDefaults. It never writes or repairs persistent storage.
ScrollSettingsLoadResult loadScrollSettingsRuntime(
    ScrollSettingsRuntimeConfig& runtime,
    ScrollSettingsPersistentReader reader);

// Production entry points. Initialization is called during boot before the
// DisplayTask exists. Reload remains explicit for a future settings API.
ScrollSettingsLoadResult initializeScrollSettingsRuntimePersistent();
ScrollSettingsLoadResult reloadScrollSettingsRuntimePersistent();

}  // namespace display_scroll
}  // namespace voxone

// Consolidated from display_scroll_api.h.
#include <stddef.h>
#include <stdint.h>


namespace voxone {
namespace display_scroll {

enum class ScrollSettingsCommandStatus : uint8_t {
  Ok,
  InvalidStructure,
  InvalidEnabled,
  InvalidSpeed,
  InvalidPause
};

ScrollSettingsCommandStatus parseScrollSettingsCommand(
    const char* value, ScrollSettings& output);

struct ScrollSettingsWebView {
  bool canScrollSettings;
  ScrollSettings settings;
  ScrollSettingsSource source;
  bool scrollCoreActive;
};

ScrollSettingsWebView makeScrollSettingsWebView(
    ScrollRendererProfile profile,
    const ScrollSettingsRuntimeSnapshot& snapshot,
    bool coreBuildEnabled);
const char* scrollSpeedApiName(ScrollSpeed speed);
const char* scrollSettingsSourceApiName(ScrollSettingsSource source);

struct ScrollSettingsApiOperations {
  void* context;
  ScrollSettingsLoadResult (*read)(void* context);
  ScrollSettingsSaveStatus (*save)(
      void* context, const ScrollSettingsLoadResult& current,
      const ScrollSettings& requested);
  ScrollSettingsLoadResult (*reload)(void* context);
  ScrollSettingsRuntimeSnapshot (*snapshot)(void* context);
};

enum class ScrollSettingsApplyStatus : uint8_t {
  Applied,
  Invalid,
  Unsupported,
  Busy,
  ReadFailed,
  StorageOpenFailed,
  StorageWriteFailed,
  StorageCommitFailed,
  StorageVerifyFailed,
  ReloadFailed,
  RuntimeVerifyFailed
};

struct ScrollSettingsApplyResult {
  ScrollSettingsApplyStatus status;
  bool changed;
  bool persisted;
  bool runtimePublished;
  bool scrollCoreActive;
};

ScrollSettingsApplyResult applyScrollSettingsUpdate(
    const ScrollSettings& requested, bool coreActive,
    const ScrollSettingsApiOperations& operations);
const char* scrollSettingsApplyError(ScrollSettingsApplyStatus status);
bool formatScrollSettingsResultJson(
    char* output, size_t capacity,
    const ScrollSettingsApplyResult& result);

// Production adapters. The view is read-only and allocation-free. The update
// performs NVS write/commit/verify, reload and runtime snapshot verification;
// callers must invoke it outside AsyncTCP and DisplayTask.
ScrollSettingsWebView currentScrollSettingsWebView();
ScrollSettingsApplyResult applyPersistentScrollSettings(
    const ScrollSettings& requested, bool coreActive);

}  // namespace display_scroll
}  // namespace voxone

#endif  // VOXONE_DISPLAY_SCROLL_H
