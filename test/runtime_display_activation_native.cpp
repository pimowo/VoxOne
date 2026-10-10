#include "../profiles/profile.h"
#include "../src/displays/display_backend_selection.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

using namespace voxone::display_backend;
using namespace voxone::display_profile;
using namespace voxone::hardware;

namespace {
#if defined(VOXONE_RUNTIME_OLED_128X64_BACKENDS)
std::string readFile(const char* path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}
#endif
}  // namespace

int main() {
  const HardwareDescriptor& hardware = currentHardware();
  const RuntimeHardwareConfig defaults = defaultRuntimeHardwareConfig();
  const CompiledBackends compiled = compiledBackends();

  Selection selected = select(defaults, hardware, compiled);
  assert(selected.display == defaults.display);
  assert(selected.controller == runtimeDisplayController(defaults.display));
  assert(selected.profile == runtimeDisplayProfile(defaults.display));

#if defined(VOXONE_RUNTIME_OLED_128X64_BACKENDS)
  assert(compiled.runtimeSelection);
  assert(compiled.contains(ControllerId::Ssd1306));
  assert(compiled.contains(ControllerId::Ssd1309));
  assert(selected.profile == ProfileId::Oled128x64);

  RuntimeHardwareConfig stored = defaults;
  assert(defaults.display == RuntimeDisplay::Ssd1306_128x64);
  assert(selected.controller == ControllerId::Ssd1306);
  stored.display = RuntimeDisplay::Ssd1309_128x64;
  assert(select(stored, hardware, compiled).controller == ControllerId::Ssd1309);
  selected = select(stored, hardware, compiled);
  assert(!selected.usedBuildDefault);
  assert(selected.profile == ProfileId::Oled128x64);

  RuntimeHardwareConfig invalid = stored;
  invalid.version = 99;
  selected = select(invalid, hardware, compiled);
  assert(selected.usedBuildDefault && selected.display == defaults.display);

  RuntimeHardwareConfig unsupported = defaults;
  unsupported.display = RuntimeDisplay::St7796s_480x320;
  selected = select(unsupported, hardware, compiled);
  assert(selected.usedBuildDefault && selected.display == defaults.display);

  const std::string facade =
      readFile("src/displays/displayOLED128x64Runtime.cpp");
  const std::string renderer = readFile("src/core/display.cpp");
  const std::string widgets =
      readFile("src/displays/widgets/widgets.h");
  const std::string m0Profile = readFile("profiles/m0.h");
  assert(facade.find("switch (activeController_)") != std::string::npos);
  assert(facade.find("ssd1306_.begin") != std::string::npos);
  assert(facade.find("ssd1309_.begin") != std::string::npos);
  assert(facade.find("tools/oledcolorfix.h") != std::string::npos);
  assert(widgets.find("_scrolldelay = millis();") != std::string::npos);
  assert(renderer.find("VOXONE_PROFILE_M0") == std::string::npos);
  assert(m0Profile.find("#define VOXONE_RUNTIME_OLED_128X64_BACKENDS 1") !=
         std::string::npos);
#else
  assert(!compiled.runtimeSelection);
  RuntimeHardwareConfig ignored = defaults;
  ignored.display = RuntimeDisplay::Ssd1306_128x64;
  selected = select(ignored, hardware, compiled);
  assert(selected.usedBuildDefault);
  assert(selected.display == defaults.display);
#if defined(VOXONE_PROFILE_W0)
  assert(selected.controller == ControllerId::St7789);
#elif defined(VOXONE_PROFILE_L0)
  assert(selected.controller == ControllerId::St7796);
#endif
#endif

  HardwareDescriptor noDisplay = hardware;
  noDisplay.displayKind = DisplayKind::None;
  noDisplay.display = {kNoPin, kNoPin, kNoPin, kNoPin, BusKind::None};
  noDisplay.capabilities.supportedDisplays = 0;
  noDisplay.capabilities.supportsEncoder = false;
  noDisplay.capabilities.supportsLocalUi = false;
  const RuntimeHardwareConfig none = defaultRuntimeHardwareConfig(noDisplay);
  const Selection headless = select(none, noDisplay, CompiledBackends{0, false});
  assert(headless.usedBuildDefault);
  assert(headless.display == RuntimeDisplay::None);
  assert(headless.controller == ControllerId::None);
  assert(headless.profile == ProfileId::None);
}
