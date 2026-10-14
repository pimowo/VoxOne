#include "display_scroll_runtime_config.h"

namespace voxone {
namespace display_scroll {
namespace {

constexpr uint8_t kEnabledShift = 0;
constexpr uint8_t kSpeedShift = 1;
constexpr uint8_t kPauseShift = 3;
constexpr uint8_t kSourceShift = 6;
constexpr uint8_t kRevisionShift = 7;
constexpr uint32_t kRevisionMask = 0x01ffffffu;

constexpr uint32_t encodeState(const ScrollSettings& settings,
                               ScrollSettingsSource source,
                               uint32_t revision) {
  return (settings.enabled ? 1u : 0u) << kEnabledShift |
         static_cast<uint32_t>(settings.speed) << kSpeedShift |
         static_cast<uint32_t>(settings.pauseMs / kPauseStepMs)
             << kPauseShift |
         static_cast<uint32_t>(source) << kSourceShift |
         (revision & kRevisionMask) << kRevisionShift;
}

ScrollSettingsRuntimeSnapshot decodeState(uint32_t state) {
  return ScrollSettingsRuntimeSnapshot{
      ScrollSettings{((state >> kEnabledShift) & 1u) != 0,
                     static_cast<ScrollSpeed>((state >> kSpeedShift) & 3u),
                     static_cast<uint16_t>(((state >> kPauseShift) & 7u) *
                                           kPauseStepMs)},
      static_cast<ScrollSettingsSource>((state >> kSourceShift) & 1u),
      (state >> kRevisionShift) & kRevisionMask};
}

bool sameRuntimeConfiguration(const ScrollSettingsRuntimeSnapshot& current,
                              const ScrollSettings& settings,
                              ScrollSettingsSource source) {
  return current.source == source &&
         equalScrollSettings(current.settings, settings);
}

ScrollSettingsLoadResult safeReadFailure() {
  return ScrollSettingsLoadResult{
      defaultScrollSettings(), ScrollSettingsSource::RendererDefaults,
      ScrollSettingsLoadStatus::RendererDefaultsReadError,
      ScrollSettingsRecordStatus::NotDecoded};
}

bool isValidatedUserSettings(const ScrollSettingsLoadResult& loaded) {
  return loaded.status == ScrollSettingsLoadStatus::UserSettings &&
         loaded.source == ScrollSettingsSource::UserSettings &&
         loaded.recordStatus == ScrollSettingsRecordStatus::Valid &&
         isValidScrollSettings(loaded.settings);
}

}  // namespace

ScrollSettingsRuntimeConfig::ScrollSettingsRuntimeConfig()
    : state_(encodeState(defaultScrollSettings(),
                         ScrollSettingsSource::RendererDefaults, 0)) {}

ScrollSettingsRuntimeSnapshot ScrollSettingsRuntimeConfig::snapshot() const {
  return decodeState(state_.load(std::memory_order_acquire));
}

ScrollSettingsPublishStatus ScrollSettingsRuntimeConfig::publish(
    const ScrollSettings& settings, ScrollSettingsSource source) {
  if (!isValidScrollSettings(settings) ||
      !isValidScrollSettingsSource(source))
    return ScrollSettingsPublishStatus::Invalid;

  uint32_t observed = state_.load(std::memory_order_acquire);
  for (;;) {
    const ScrollSettingsRuntimeSnapshot current = decodeState(observed);
    if (sameRuntimeConfiguration(current, settings, source))
      return ScrollSettingsPublishStatus::Unchanged;
    const uint32_t nextRevision =
        (current.revision + 1u) & kRevisionMask;
    const uint32_t desired = encodeState(settings, source, nextRevision);
    if (state_.compare_exchange_weak(observed, desired,
                                     std::memory_order_release,
                                     std::memory_order_acquire))
      return ScrollSettingsPublishStatus::Changed;
  }
}

ScrollSettingsRuntimeConfig& scrollSettingsRuntimeConfig() {
  static ScrollSettingsRuntimeConfig runtime;
  return runtime;
}

ScrollSettingsLoadResult loadScrollSettingsRuntime(
    ScrollSettingsRuntimeConfig& runtime,
    ScrollSettingsPersistentReader reader) {
  const ScrollSettingsLoadResult loaded = reader ? reader() : safeReadFailure();
  if (isValidatedUserSettings(loaded)) {
    (void)runtime.publish(loaded.settings, ScrollSettingsSource::UserSettings);
  } else {
    (void)runtime.publish(defaultScrollSettings(),
                          ScrollSettingsSource::RendererDefaults);
  }
  return loaded;
}

}  // namespace display_scroll
}  // namespace voxone
