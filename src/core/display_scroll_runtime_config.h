#ifndef VOXONE_DISPLAY_SCROLL_RUNTIME_CONFIG_H
#define VOXONE_DISPLAY_SCROLL_RUNTIME_CONFIG_H

#include <atomic>
#include <stdint.h>

#include "display_scroll_persistence.h"

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

#endif
