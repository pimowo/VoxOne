#include "display_scroll_api.h"

#include "player_scroll_runtime.h"
#include "../displays/display_backend_selection.h"
#include "../hardware/hardware_descriptor.h"
#include "../hardware/runtime_hardware_config.h"

namespace voxone {
namespace display_scroll {
namespace {

ScrollRendererProfile scrollProfile(
    display_profile::ProfileId profile) {
  switch (profile) {
    case display_profile::ProfileId::Oled128x64:
      return ScrollRendererProfile::Oled128x64;
    case display_profile::ProfileId::St7789_284x76:
      return ScrollRendererProfile::St7789_284x76;
    case display_profile::ProfileId::St7796_480x320:
      return ScrollRendererProfile::St7796_480x320;
    case display_profile::ProfileId::None:
      return ScrollRendererProfile::None;
  }
  return ScrollRendererProfile::None;
}

ScrollSettingsLoadResult productionRead(void*) {
  return readScrollSettingsPersistent();
}

ScrollSettingsSaveStatus productionSave(
    void*, const ScrollSettingsLoadResult& current,
    const ScrollSettings& requested) {
  return writeScrollSettingsPersistent(current, requested);
}

ScrollSettingsLoadResult productionReload(void*) {
  return reloadScrollSettingsRuntimePersistent();
}

ScrollSettingsRuntimeSnapshot productionSnapshot(void*) {
  return scrollSettingsRuntimeConfig().snapshot();
}

}  // namespace

ScrollSettingsWebView currentScrollSettingsWebView() {
  const display_backend::Selection selected = display_backend::select(
      hardware::runtimeHardwareConfig(), hardware::currentHardware(),
      display_backend::compiledBackends());
  return makeScrollSettingsWebView(
      scrollProfile(selected.profile), scrollSettingsRuntimeConfig().snapshot(),
      playerScrollCoreBuildEnabled());
}

ScrollSettingsApplyResult applyPersistentScrollSettings(
    const ScrollSettings& requested, bool coreActive) {
  const ScrollSettingsApiOperations operations{
      nullptr, productionRead, productionSave, productionReload,
      productionSnapshot};
  return applyScrollSettingsUpdate(requested, coreActive, operations);
}

}  // namespace display_scroll
}  // namespace voxone
