#include "../profiles/profile.h"
#include "../src/core/local_input_backend.h"
#include "../src/displays/display_backend_selection.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

using namespace voxone::display_backend;
using namespace voxone::display_profile;
using namespace voxone::hardware;

namespace {
#if defined(VOXONE_RUNTIME_OLED_128X64_BACKENDS) || \
    defined(VOXONE_RUNTIME_L0_DISPLAY_BACKENDS)
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

  RuntimeHardwareConfig noneDisplay = defaults;
  noneDisplay.display = RuntimeDisplay::None;
  assert(validateRuntimeHardwareConfig(noneDisplay, hardware.capabilities) ==
         RuntimeHardwareValidation::Valid);
  selected = select(noneDisplay, hardware, compiled);
  assert(!selected.usedBuildDefault);
  assert(selected.display == RuntimeDisplay::None);
  assert(selected.controller == ControllerId::None);
  assert(selected.profile == ProfileId::None);
  const ActivationPlan headlessPlan = activationPlan(selected);
  assert(!headlessPlan.initializeBackend);
  assert(!headlessPlan.allocateFramebuffer);
  assert(!headlessPlan.runRenderer);
  assert(headlessPlan.createDisplayTask);
  assert(rendererKind(selected) == RendererKind::None);

  RuntimeHardwareConfig noneInput = defaults;
  noneInput.input = RuntimeInput::None;
  assert(validateRuntimeHardwareConfig(noneInput, hardware.capabilities) ==
         RuntimeHardwareValidation::Valid);
  const auto noInputActivation =
      voxone::input::activationFor(noneInput.input);
  assert(!noInputActivation.ec11 && !noInputActivation.buttons3);

  RuntimeHardwareConfig noneBoth = noneDisplay;
  noneBoth.input = RuntimeInput::None;
  assert(validateRuntimeHardwareConfig(noneBoth, hardware.capabilities) ==
         RuntimeHardwareValidation::Valid);
  assert(select(noneBoth, hardware, compiled).controller ==
         ControllerId::None);

  RuntimeHardwareConfig noDisplayEc11 = noneDisplay;
  noDisplayEc11.input = RuntimeInput::Ec11;
  assert(validateRuntimeHardwareConfig(noDisplayEc11,
                                       hardware.capabilities) ==
         RuntimeHardwareValidation::Valid);
  RuntimeHardwareConfig displayNoInput = defaults;
  displayNoInput.input = RuntimeInput::None;
  assert(validateRuntimeHardwareConfig(displayNoInput,
                                       hardware.capabilities) ==
         RuntimeHardwareValidation::Valid);

#if defined(VOXONE_RUNTIME_OLED_128X64_BACKENDS) || \
    defined(VOXONE_RUNTIME_L0_DISPLAY_BACKENDS)
  selected = select(defaults, hardware, compiled);
  assert(compiled.runtimeSelection);
  assert(compiled.contains(ControllerId::Ssd1306));
  assert(compiled.contains(ControllerId::Ssd1309));
  assert(compiled.contains(ControllerId::Sh1106));
  assert(compiled.contains(ControllerId::Ch1116g));
#if defined(VOXONE_PROFILE_S0)
  assert(selected.profile == ProfileId::Oled128x32);
#elif defined(VOXONE_PROFILE_L0)
  assert(compiled.contains(ControllerId::St7796));
  assert(defaults.display == RuntimeDisplay::St7796s_480x320);
  assert(selected.controller == ControllerId::St7796);
  assert(selected.profile == ProfileId::St7796_480x320);
  assert(rendererKind(selected) == RendererKind::St7796);
#else
  assert(selected.profile == ProfileId::Oled128x64);
#endif

  RuntimeHardwareConfig stored = defaults;
#if defined(VOXONE_PROFILE_S0)
  assert(defaults.display == RuntimeDisplay::Ssd1306_128x32);
  assert(selected.controller == ControllerId::Ssd1306);
  stored.display = RuntimeDisplay::Ssd1306_128x64;
  assert(select(stored, hardware, compiled).controller == ControllerId::Ssd1306);
#elif defined(VOXONE_PROFILE_L0)
  stored.display = RuntimeDisplay::Ssd1306_128x64;
  assert(select(stored, hardware, compiled).controller == ControllerId::Ssd1306);
#else
  assert(defaults.display == RuntimeDisplay::Ssd1306_128x64);
  assert(selected.controller == ControllerId::Ssd1306);
  stored.display = RuntimeDisplay::Ssd1309_128x64;
  assert(select(stored, hardware, compiled).controller == ControllerId::Ssd1309);
#endif
  selected = select(stored, hardware, compiled);
  assert(!selected.usedBuildDefault);
  assert(selected.profile == ProfileId::Oled128x64);

  for (RuntimeDisplay display :
       {RuntimeDisplay::Ssd1306_128x32,
        RuntimeDisplay::Ssd1306_128x64,
        RuntimeDisplay::Ssd1309_128x64,
        RuntimeDisplay::Ch1116g_128x64,
        RuntimeDisplay::Sh1106_128x64}) {
    RuntimeHardwareConfig oled = defaults;
    oled.display = display;
    assert(validateRuntimeHardwareConfig(oled, hardware.capabilities) ==
           RuntimeHardwareValidation::Valid);
    const Selection runtime = select(oled, hardware, compiled);
    assert(!runtime.usedBuildDefault && runtime.display == display);
    assert(runtime.profile ==
           (display == RuntimeDisplay::Ssd1306_128x32
                ? ProfileId::Oled128x32 : ProfileId::Oled128x64));
    assert(rendererKind(runtime) ==
           (display == RuntimeDisplay::Ssd1306_128x32
                ? RendererKind::Oled128x32 : RendererKind::Oled128x64));
  }

#if defined(VOXONE_PROFILE_L0)
  RuntimeHardwareConfig st = defaults;
  st.display = RuntimeDisplay::St7796s_480x320;
  const Selection runtimeSt = select(st, hardware, compiled);
  assert(!runtimeSt.usedBuildDefault);
  assert(runtimeSt.controller == ControllerId::St7796);
  assert(rendererKind(runtimeSt) == RendererKind::St7796);
#endif

  RuntimeHardwareConfig invalid = stored;
  invalid.version = 99;
  selected = select(invalid, hardware, compiled);
  assert(selected.usedBuildDefault && selected.display == defaults.display);

  RuntimeHardwareConfig unsupported = defaults;
#if defined(VOXONE_PROFILE_L0)
  unsupported.display = RuntimeDisplay::St7789_284x76;
#else
  unsupported.display = RuntimeDisplay::St7796s_480x320;
#endif
  selected = select(unsupported, hardware, compiled);
  assert(selected.usedBuildDefault && selected.display == defaults.display);

  const std::string facade =
      readFile("src/displays/displayOLED128x64Runtime.cpp");
  const std::string renderer = readFile("src/core/display.cpp");
  const std::string controls = readFile("src/core/controls.cpp");
  const std::string config = readFile("src/core/config.cpp");
  const std::string widgets =
      readFile("src/displays/widgets/widgets.h");
  const std::string m0Profile = readFile("profiles/m0.h");
  assert(facade.find("switch (activeController_)") != std::string::npos);
  assert(facade.find("activeSsd1306()->begin") != std::string::npos);
  assert(facade.find("ssd1309_.begin") != std::string::npos);
  assert(facade.find("sh1106_.begin") != std::string::npos);
  assert(facade.find("ch1116g_->begin") != std::string::npos);
  assert(facade.find("ControllerId::Ch1116g") != std::string::npos);
  assert(facade.find("SH1106_I2CFREQ_HZ 400000UL") != std::string::npos);
  assert(facade.find("tools/oledcolorfix.h") != std::string::npos);
  assert(widgets.find("_scrolldelay = millis();") != std::string::npos);
  assert(renderer.find("VOXONE_PROFILE_M0") == std::string::npos);
  const size_t noneGuard = renderer.find("if (!_runtimeEnabled)");
  const size_t backendInit = renderer.find("dsp.initDisplay()");
  const size_t rendererAllocation =
      renderer.find("_pager = new (std::nothrow) Pager()");
  const size_t rendererAllocationFailure =
      renderer.find("if (!_pager)", rendererAllocation);
  assert(noneGuard != std::string::npos && backendInit != std::string::npos &&
         rendererAllocation != std::string::npos &&
         rendererAllocationFailure != std::string::npos);
  assert(noneGuard < backendInit && backendInit < rendererAllocation);
  assert(rendererAllocation < rendererAllocationFailure);
  assert(controls.find("activationFor(selected)") != std::string::npos);
  assert(m0Profile.find("#define VOXONE_RUNTIME_OLED_128X64_BACKENDS 1") !=
         std::string::npos);
#if defined(VOXONE_PROFILE_L0)
  const std::string l0Facade = readFile("src/displays/displayL0Runtime.cpp");
  const std::string l0Header = readFile("src/displays/displayL0Runtime.h");
  const std::string l0Profile = readFile("profiles/l0.h");
  const std::string rtcSupport = readFile("src/core/rtcsupport.cpp");
  assert(l0Facade.find("selection_.controller == ControllerId::St7796") !=
         std::string::npos);
  assert(l0Facade.find("else if (oledActive())") != std::string::npos);
  assert(l0Facade.find("RuntimeOled128x64Display(RTCWire)") !=
         std::string::npos);
  assert(l0Facade.find("../core/rtcsupport.h") != std::string::npos);
  assert(facade.find("if (!initRTCWire()) return false") !=
         std::string::npos);
  assert(rtcSupport.find("if(rtcWireInitialized) return true") !=
         std::string::npos);
  assert(rtcSupport.find("rtcWireInitialized = RTCWire.begin") !=
         std::string::npos);
  assert(l0Header.find("Adafruit_ST7796S_kbv* st7796_ = nullptr") !=
         std::string::npos);
  assert(l0Header.find("RuntimeOled128x64Display* oled_ = nullptr") !=
         std::string::npos);
  assert(l0Profile.find("#define I2CFREQ_HZ 400000UL") !=
         std::string::npos);
  assert(renderer.find("_st7796PlayerReady = !smallOledActive()") !=
         std::string::npos);
  assert(config.find("bool localBacklightInstalled()") != std::string::npos);
  assert(config.find("RuntimeDisplay::St7796s_480x320") !=
         std::string::npos);
  assert(renderer.find("bool enableBacklight") != std::string::npos);
#endif
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
  noDisplay.capabilities.supportedInputs = 0;
  noDisplay.capabilities.supportsLocalUi = false;
  const RuntimeHardwareConfig none = defaultRuntimeHardwareConfig(noDisplay);
  const Selection headless = select(none, noDisplay, CompiledBackends{0, false});
  assert(!headless.usedBuildDefault);
  assert(headless.display == RuntimeDisplay::None);
  assert(headless.controller == ControllerId::None);
  assert(headless.profile == ProfileId::None);
}
