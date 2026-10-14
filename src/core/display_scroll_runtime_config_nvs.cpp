#include "display_scroll_runtime_config.h"

namespace voxone {
namespace display_scroll {

ScrollSettingsLoadResult initializeScrollSettingsRuntimePersistent() {
  return loadScrollSettingsRuntime(scrollSettingsRuntimeConfig(),
                                   readScrollSettingsPersistent);
}

ScrollSettingsLoadResult reloadScrollSettingsRuntimePersistent() {
  return loadScrollSettingsRuntime(scrollSettingsRuntimeConfig(),
                                   readScrollSettingsPersistent);
}

}  // namespace display_scroll
}  // namespace voxone
