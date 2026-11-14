#include "../src/hardware/runtime_hardware_web.h"
#include "../profiles/profile.h"

#include <cassert>
#include <cstring>
#include <string>

using namespace voxone::hardware;

namespace {

class FakeStorage : public RuntimeHardwareStorage {
 public:
  RuntimeHardwareStorageReadStatus read(uint8_t* output, size_t capacity,
                                        size_t& outputSize) override {
    if (!present) return RuntimeHardwareStorageReadStatus::Missing;
    outputSize = size;
    if (size <= capacity) std::memcpy(output, bytes, size);
    return RuntimeHardwareStorageReadStatus::Found;
  }

  bool write(const uint8_t* input, size_t inputSize) override {
    if (inputSize > sizeof(bytes)) return false;
    std::memcpy(bytes, input, inputSize);
    size = inputSize;
    present = true;
    ++writes;
    return true;
  }

  uint8_t bytes[32]{};
  size_t size = 0;
  unsigned writes = 0;
  bool present = false;
};

bool contains(const std::string& text, const std::string& value) {
  return text.find(value) != std::string::npos;
}

bool hasOption(const RuntimeHardwareWebOption* options, size_t count,
               const char* value) {
  for (size_t index = 0; index < count; ++index)
    if (std::strcmp(options[index].value, value) == 0) return true;
  return false;
}

const HardwareRoadmapItem* findRoadmapItem(
    const HardwareRoadmapItem* items, size_t count, const char* value) {
  for (size_t index = 0; index < count; ++index)
    if (std::strcmp(items[index].value, value) == 0) return &items[index];
  return nullptr;
}

void assertRoadmapOrder(const HardwareRoadmapItem* items, size_t count,
                        const char* const* expected, size_t expectedCount) {
  assert(count == expectedCount);
  for (size_t index = 0; index < count; ++index)
    assert(std::strcmp(items[index].value, expected[index]) == 0);
}

}  // namespace

int main() {
  const HardwareDescriptor& hardware = currentHardware();
  const HardwareCapabilities& capabilities = hardwareCapabilities();
  const RuntimeHardwareConfig current = defaultRuntimeHardwareConfig();

  RuntimeHardwareWebOption displays[kRuntimeDisplayOptionCapacity]{};
  const size_t displayCount =
      supportedRuntimeDisplayOptions(capabilities, displays,
                                     kRuntimeDisplayOptionCapacity);
  assert(displayCount == 2 || displayCount == 5 || displayCount == 7);
  assert(std::strcmp(displays[0].value, "none") == 0);
  assert(std::strcmp(displays[0].label, "Brak") == 0);
#if defined(VOXONE_PROFILE_S0) || defined(VOXONE_PROFILE_DIN0)
  assert(displayCount == 5);
  assert(hasOption(displays, displayCount, "ssd1306_128x64"));
  assert(hasOption(displays, displayCount, "ssd1309_128x64"));
  assert(hasOption(displays, displayCount, "sh1106_128x64"));
  assert(hasOption(displays, displayCount, "ch1116_128x64"));
  assert(!hasOption(displays, displayCount, "ssd1306_128x32"));
  assert(!hasOption(displays, displayCount, "st7796s_480x320"));
  const char* const expectedRuntimeOledOptions[] = {
      "none", "ssd1306_128x64",
      "ssd1309_128x64", "ch1116_128x64", "sh1106_128x64"};
  for (size_t index = 0; index < displayCount; ++index)
    assert(std::strcmp(displays[index].value,
                       expectedRuntimeOledOptions[index]) == 0);
#elif defined(VOXONE_PROFILE_L0) || defined(VOXONE_PROFILE_M0)
  assert(displayCount == 7);
  assert(hasOption(displays, displayCount, "st7796s_480x320"));
  assert(hasOption(displays, displayCount, "ssd1322_256x64"));
  assert(hasOption(displays, displayCount, "gc9a01_240x240"));
  assert(hasOption(displays, displayCount, "ssd1306_128x64"));
  assert(hasOption(displays, displayCount, "ssd1309_128x64"));
  assert(hasOption(displays, displayCount, "sh1106_128x64"));
  assert(!hasOption(displays, displayCount, "ch1116_128x64"));
  assert(!hasOption(displays, displayCount, "ssd1306_128x32"));
  const char* const expectedL0DisplayOptions[] = {
      "none", "ssd1306_128x64", "ssd1309_128x64",
      "sh1106_128x64", "st7796s_480x320", "ssd1322_256x64",
      "gc9a01_240x240"};
  for (size_t index = 0; index < displayCount; ++index)
    assert(std::strcmp(displays[index].value,
                       expectedL0DisplayOptions[index]) == 0);
#endif

  RuntimeHardwareWebOption inputs[3]{};
  const size_t inputCount = supportedRuntimeInputOptions(capabilities, inputs, 3);
  assert(inputCount == 3);
  assert(std::strcmp(inputs[0].value, "none") == 0);
  assert(std::strcmp(inputs[0].label, "Brak") == 0);
  assert(hasOption(inputs, inputCount, "ec11"));
  assert(hasOption(inputs, inputCount, "buttons3"));

  RuntimeHardwareWebOption audioOutputs[5]{};
  const size_t audioOutputCount =
      supportedRuntimeAudioOutputOptions(capabilities, audioOutputs, 5);
  assert(std::strcmp(audioOutputs[0].value, "pcm5102a") == 0);
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0)
  assert(std::strcmp(audioOutputs[0].label, "PCM5102 (Domyślny)") == 0);
  assert(audioOutputCount == 4);
  assert(hasOption(audioOutputs, audioOutputCount, "max98357a"));
  assert(hasOption(audioOutputs, audioOutputCount, "max98357a_stereo"));
  const RuntimeHardwareWebOption* mono = nullptr;
  const RuntimeHardwareWebOption* stereo = nullptr;
  for (size_t index = 0; index < audioOutputCount; ++index) {
    if (std::strcmp(audioOutputs[index].value, "max98357a") == 0)
      mono = &audioOutputs[index];
    if (std::strcmp(audioOutputs[index].value, "max98357a_stereo") == 0)
      stereo = &audioOutputs[index];
  }
  assert(mono && std::strcmp(mono->label,
                            "MAX98357A MONO (1 moduł)") == 0);
  assert(stereo && std::strcmp(stereo->label,
                              "MAX98357A STEREO (2 moduły)") == 0);
  assert(hasOption(audioOutputs, audioOutputCount, "none"));
#elif defined(VOXONE_PROFILE_L0)
  assert(std::strcmp(audioOutputs[0].label, "PCM5102 (Domyślny)") == 0);
  assert(audioOutputCount == 3);
  assert(!hasOption(audioOutputs, audioOutputCount, "max98357a"));
  assert(hasOption(audioOutputs, audioOutputCount, "dspmini"));
  assert(hasOption(audioOutputs, audioOutputCount, "none"));
#else
  assert(std::strcmp(audioOutputs[0].label, "PCM5102 (Domyślny)") == 0);
  assert(audioOutputCount == 1);
  assert(std::strcmp(audioOutputs[0].value, "pcm5102a") == 0);
  assert(!hasOption(audioOutputs, audioOutputCount, "max98357a"));
  assert(!hasOption(audioOutputs, audioOutputCount, "none"));
#endif
  assert(hasOption(audioOutputs, audioOutputCount, "dspmini") ==
#if defined(VOXONE_PROFILE_L0)
         true);
#else
         false);
#endif
  assert(hasOption(audioOutputs, audioOutputCount, "max98357a_stereo") ==
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0)
         true);
#else
         false);
#endif

  assert(std::strcmp(hardwareRoadmapStatusValue(
                         HardwareRoadmapStatus::Supported),
                     "supported") == 0);
  assert(std::strcmp(hardwareRoadmapStatusLabel(
                         HardwareRoadmapStatus::Untested),
                     "DO TESTU") == 0);
  assert(std::strcmp(hardwareRoadmapStatusLabel(
                         HardwareRoadmapStatus::Planned),
                     "PLANOWANE") == 0);

  HardwareRoadmapItem displayRoadmap[12]{};
  const size_t displayRoadmapCount =
      hardwareDisplayRoadmap(hardware, current, displayRoadmap, 12);
#if defined(VOXONE_PROFILE_L0) || defined(VOXONE_PROFILE_M0)
  const char* const expectedDisplays[] = {
      "none", "ssd1306_128x32", "ssd1306_128x64", "ssd1309_128x64",
      "ch1116_128x64", "sh1106_128x64", "st7290_128x64_tbd",
      "st7789v_320x240", "ssd1322_256x64", "st7796s_480x320",
      "gc9a01_240x240"};
#elif defined(VOXONE_PROFILE_S0) || defined(VOXONE_PROFILE_DIN0)
  const char* const expectedDisplays[] = {
      "none", "ssd1306_128x32", "ssd1306_128x64",
      "ssd1309_128x64", "ch1116_128x64", "sh1106_128x64"};
#endif
  assertRoadmapOrder(displayRoadmap, displayRoadmapCount, expectedDisplays,
                     sizeof(expectedDisplays) / sizeof(expectedDisplays[0]));
  const HardwareRoadmapItem* noneDisplay =
      findRoadmapItem(displayRoadmap, displayRoadmapCount, "none");
  assert(noneDisplay && noneDisplay->selectable);
  assert(noneDisplay->status == HardwareRoadmapStatus::Supported);
  const HardwareRoadmapItem* plannedDisplay = findRoadmapItem(
      displayRoadmap, displayRoadmapCount, "ch1116_128x64");
  assert(plannedDisplay);
#if defined(VOXONE_PROFILE_S0) || defined(VOXONE_PROFILE_DIN0)
  assert(plannedDisplay->selectable);
  assert(plannedDisplay->status == HardwareRoadmapStatus::Untested);
#else
  assert(!plannedDisplay->selectable);
  assert(plannedDisplay->status == HardwareRoadmapStatus::Planned);
#endif
  const HardwareRoadmapItem* currentDisplay = findRoadmapItem(
      displayRoadmap, displayRoadmapCount,
      runtimeDisplayWebValue(current.display));
  assert(currentDisplay && currentDisplay->selectable && currentDisplay->current);

  HardwareRoadmapItem inputRoadmap[3]{};
  const size_t inputRoadmapCount =
      hardwareInputRoadmap(hardware, current, inputRoadmap, 3);
  const char* const expectedInputs[] = {"none", "ec11", "buttons3"};
  assertRoadmapOrder(inputRoadmap, inputRoadmapCount, expectedInputs, 3);
  const HardwareRoadmapItem* buttons =
      findRoadmapItem(inputRoadmap, inputRoadmapCount, "buttons3");
  assert(buttons && buttons->selectable);
  assert(buttons->status == HardwareRoadmapStatus::Untested);
  assert(std::strcmp(buttons->note, "Brak fizycznego PASS") == 0);

  const RuntimeHardwareConfig beforeInformationalRoadmaps = current;
  HardwareRoadmapItem audioRoadmap[5]{};
  HardwareRoadmapItem moduleRoadmap[4]{};
  const size_t audioRoadmapCount =
      hardwareAudioRoadmap(hardware, current, audioRoadmap, 5);
  const size_t moduleRoadmapCount =
      hardwareModuleRoadmap(hardware, moduleRoadmap, 4);
#if defined(VOXONE_PROFILE_L0)
  const char* const expectedAudio[] = {"pcm5102a", "dspmini", "none"};
  const char* const expectedModules[] = {"voxonebt", "ds3231", "w5500"};
#elif defined(VOXONE_PROFILE_M0)
  const char* const expectedAudio[] = {
      "pcm5102a", "max98357a", "max98357a_stereo", "none"};
  const char* const expectedModules[] = {"voxonebt", "ds3231", "battery"};
#elif defined(VOXONE_PROFILE_S0)
  const char* const expectedAudio[] = {
      "pcm5102a", "max98357a", "max98357a_stereo", "none"};
  const char* const expectedModules[] = {"voxonebt", "ds3231", "battery"};
#elif defined(VOXONE_PROFILE_DIN0)
  const char* const expectedAudio[] = {"pcm5102a"};
  const char* const expectedModules[] = {"voxonebt"};
#endif
  assertRoadmapOrder(audioRoadmap, audioRoadmapCount, expectedAudio,
                     sizeof(expectedAudio) / sizeof(expectedAudio[0]));
  assertRoadmapOrder(moduleRoadmap, moduleRoadmapCount, expectedModules,
                     sizeof(expectedModules) / sizeof(expectedModules[0]));
  for (size_t index = 0; index < audioRoadmapCount; ++index)
    assert(audioRoadmap[index].selectable ==
           capabilities.supportsAudioOutput(
               std::strcmp(audioRoadmap[index].value, "pcm5102a") == 0
                   ? AudioOutputKind::Pcm5102a
                   : std::strcmp(audioRoadmap[index].value, "max98357a") == 0
                         ? AudioOutputKind::Max98357
                         : std::strcmp(audioRoadmap[index].value,
                                       "max98357a_stereo") == 0
                               ? AudioOutputKind::Max98357Stereo
                         : std::strcmp(audioRoadmap[index].value, "dspmini") == 0
                               ? AudioOutputKind::DspMini
                               : AudioOutputKind::None));
  for (size_t index = 0; index < moduleRoadmapCount; ++index)
    assert(!moduleRoadmap[index].selectable);
#if defined(VOXONE_PROFILE_L0) || defined(VOXONE_PROFILE_M0)
  assert(findRoadmapItem(audioRoadmap, audioRoadmapCount,
                         "pcm5102a")->current);
#endif
#if defined(VOXONE_PROFILE_S0)
  assert(findRoadmapItem(audioRoadmap, audioRoadmapCount,
                         "max98357a")->current);
  assert(audioRoadmap[0].status == HardwareRoadmapStatus::Untested);
#endif
  assert(current == beforeInformationalRoadmaps);

  char json[kRuntimeHardwareWebJsonCapacity];
  assert(formatRuntimeHardwareWebJson(json, sizeof(json), hardware, current));
  const std::string snapshot(json);
  assert(contains(snapshot, std::string("\"family\":\"") +
                                hardware.board.name + "\""));
  assert(contains(snapshot, "\"config\":{"));
  assert(contains(snapshot, runtimeDisplayWebValue(current.display)));
  assert(contains(snapshot, runtimeInputWebValue(current.input)));
  assert(contains(snapshot, runtimeAudioOutputWebValue(current.audio)));
  assert(contains(snapshot, "\"supportedDisplays\":["));
  assert(contains(snapshot, "\"supportedInputs\":["));
  assert(contains(snapshot, "\"supportedAudioOutputs\":["));
  assert(contains(snapshot, "\"roadmap\":{\"displays\":["));
  assert(contains(snapshot, "\"audio\":["));
  assert(contains(snapshot, "\"modules\":["));
  assert(contains(snapshot, "\"status\":\"planned\""));
  assert(contains(snapshot, "\"status\":\"untested\""));
  assert(contains(snapshot, "\"restartRequired\":false"));
  char shortJson[32];
  assert(!formatRuntimeHardwareWebJson(shortJson, sizeof(shortJson), hardware,
                                       current));

  RuntimeHardwareConfig candidate{};
  assert(makeRuntimeHardwareWebCandidate(current, capabilities, "none", "none",
                                         candidate) ==
         RuntimeHardwareValidation::Valid);
  assert(candidate.display == RuntimeDisplay::None);
  assert(candidate.input == RuntimeInput::None);
  assert(candidate.audio == current.audio);

  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, "none", "none", "pcm5102a",
             candidate) == RuntimeHardwareValidation::Valid);
  assert(candidate.display == RuntimeDisplay::None);
  assert(candidate.input == RuntimeInput::None);
  assert(candidate.audio == RuntimeAudioOutput::Pcm5102a);
  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, "sh1106_128x64", "ec11",
             runtimeAudioOutputWebValue(current.audio), candidate) ==
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0) || \
    defined(VOXONE_PROFILE_DIN0) || defined(VOXONE_PROFILE_L0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif
  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, runtimeDisplayWebValue(current.display),
             "ec11", "none", candidate) ==
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0) || \
    defined(VOXONE_PROFILE_L0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif
  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, runtimeDisplayWebValue(current.display),
             "ec11", "bad", candidate) ==
         RuntimeHardwareValidation::Invalid);
  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, runtimeDisplayWebValue(current.display),
             "ec11", "max98357a", candidate) ==
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0)
  assert(candidate.audio == RuntimeAudioOutput::Max98357);
#endif
  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, runtimeDisplayWebValue(current.display),
             "ec11", "max98357a_stereo", candidate) ==
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0)
  assert(candidate.audio == RuntimeAudioOutput::Max98357Stereo);
#endif
  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, runtimeDisplayWebValue(current.display),
             "ec11", "dspmini", candidate) ==
#if defined(VOXONE_PROFILE_L0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif

  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, runtimeDisplayWebValue(current.display),
             "buttons3", candidate) == RuntimeHardwareValidation::Valid);
  assert(candidate.display == current.display);
  assert(candidate.input == RuntimeInput::Buttons3);

  assert(makeRuntimeHardwareWebCandidate(current, capabilities, "bad", "ec11",
                                         candidate) ==
         RuntimeHardwareValidation::Invalid);
  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities,
             "ch1116_128x64",
             "ec11", candidate) ==
#if defined(VOXONE_PROFILE_S0) || defined(VOXONE_PROFILE_DIN0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif
  assert(makeRuntimeHardwareWebCandidate(current, capabilities,
                                         runtimeDisplayWebValue(current.display),
                                         "bad", candidate) ==
         RuntimeHardwareValidation::Invalid);

  HardwareCapabilities limited = capabilities;
  limited.supportedInputs = inputSupport(LocalInputKind::Ec11);
  assert(makeRuntimeHardwareWebCandidate(current, limited,
                                         runtimeDisplayWebValue(current.display),
                                         "buttons3", candidate) ==
         RuntimeHardwareValidation::Unsupported);
  assert(makeRuntimeHardwareWebCandidate(current, limited, "none", "buttons3",
                                         candidate) ==
         RuntimeHardwareValidation::Unsupported);

  const char* unsupportedDisplay = nullptr;
  for (const char* value : {"ssd1306_128x32", "ssd1306_128x64",
                            "ssd1309_128x64", "ch1116_128x64",
                            "sh1106_128x64",
                            "st7789_284x76", "st7796s_480x320"}) {
    RuntimeDisplay parsed = RuntimeDisplay::None;
    assert(parseRuntimeDisplayWebValue(value, parsed));
    DisplayKind kind = DisplayKind::None;
    assert(runtimeDisplayKind(parsed, kind));
    if (!capabilities.supportsDisplay(kind)) {
      unsupportedDisplay = value;
      break;
    }
  }
  assert(unsupportedDisplay);
  assert(makeRuntimeHardwareWebCandidate(current, capabilities,
                                         unsupportedDisplay, "ec11",
                                         candidate) ==
         RuntimeHardwareValidation::Unsupported);

  FakeStorage rejectedStorage;
  assert(saveRuntimeHardwareConfig(rejectedStorage, current, candidate,
                                   capabilities) ==
         RuntimeHardwareSaveStatus::Unsupported);
  assert(rejectedStorage.writes == 0);

  RuntimeHardwareConfig noDisplay = current;
  noDisplay.display = RuntimeDisplay::None;
  FakeStorage displayNoneStorage;
  assert(saveRuntimeHardwareConfig(displayNoneStorage, current, noDisplay,
                                   capabilities) ==
         RuntimeHardwareSaveStatus::Saved);
  assert(loadRuntimeHardwareConfig(displayNoneStorage, hardware).config ==
         noDisplay);

  FakeStorage displayStorage;
  assert(saveRuntimeHardwareConfig(displayStorage, noDisplay, current,
                                   capabilities) ==
         RuntimeHardwareSaveStatus::Saved);
  assert(loadRuntimeHardwareConfig(displayStorage, hardware).config == current);

#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0) || \
    defined(VOXONE_PROFILE_DIN0) || defined(VOXONE_PROFILE_L0)
  RuntimeHardwareConfig sh1106 = current;
  sh1106.display = RuntimeDisplay::Sh1106_128x64;
  FakeStorage sh1106Storage;
  assert(saveRuntimeHardwareConfig(sh1106Storage, current, sh1106,
                                   capabilities) ==
         RuntimeHardwareSaveStatus::Saved);
  const RuntimeHardwareLoadResult reloadedSh1106 =
      loadRuntimeHardwareConfig(sh1106Storage, hardware);
  assert(reloadedSh1106.status == RuntimeHardwareLoadStatus::Loaded);
  assert(reloadedSh1106.config == sh1106);
  char sh1106Json[192];
  assert(formatRuntimeHardwareWebSaveJson(
      sh1106Json, sizeof(sh1106Json), reloadedSh1106.config, true));
  assert(contains(sh1106Json,
                  "\"display\":\"sh1106_128x64\""));
#endif

#if defined(VOXONE_PROFILE_S0) || defined(VOXONE_PROFILE_DIN0)
  RuntimeHardwareConfig ch1116g = current;
  ch1116g.display = RuntimeDisplay::Ch1116g_128x64;
  FakeStorage ch1116gStorage;
  assert(saveRuntimeHardwareConfig(ch1116gStorage, current, ch1116g,
                                   capabilities) ==
         RuntimeHardwareSaveStatus::Saved);
  const RuntimeHardwareLoadResult reloadedCh1116g =
      loadRuntimeHardwareConfig(ch1116gStorage, hardware);
  assert(reloadedCh1116g.status == RuntimeHardwareLoadStatus::Loaded);
  assert(reloadedCh1116g.config == ch1116g);
  char ch1116gJson[192];
  assert(formatRuntimeHardwareWebSaveJson(
      ch1116gJson, sizeof(ch1116gJson), reloadedCh1116g.config, true));
  assert(contains(ch1116gJson,
                  "\"display\":\"ch1116_128x64\""));
#endif

  RuntimeHardwareConfig noInput = current;
  noInput.input = RuntimeInput::None;
  FakeStorage inputNoneStorage;
  assert(saveRuntimeHardwareConfig(inputNoneStorage, current, noInput,
                                   capabilities) ==
         RuntimeHardwareSaveStatus::Saved);
  assert(loadRuntimeHardwareConfig(inputNoneStorage, hardware).config == noInput);

  RuntimeHardwareConfig buttonsInput = noInput;
  buttonsInput.input = RuntimeInput::Buttons3;
  FakeStorage inputStorage;
  assert(saveRuntimeHardwareConfig(inputStorage, noInput, buttonsInput,
                                   capabilities) ==
         RuntimeHardwareSaveStatus::Saved);
  assert(loadRuntimeHardwareConfig(inputStorage, hardware).config ==
         buttonsInput);

  FakeStorage storage;
  RuntimeHardwareConfig requested = current;
  requested.display = RuntimeDisplay::None;
  requested.input = RuntimeInput::None;
  assert(saveRuntimeHardwareConfig(storage, current, requested, capabilities) ==
         RuntimeHardwareSaveStatus::Saved);
  assert(storage.writes == 1);
  const RuntimeHardwareLoadResult loaded =
      loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::Loaded);
  assert(loaded.config == requested);

  char savedJson[192];
  assert(formatRuntimeHardwareWebSaveJson(savedJson, sizeof(savedJson),
                                          requested, true));
  const std::string saved(savedJson);
  assert(contains(saved, "\"ok\":true"));
  assert(contains(saved, "\"changed\":true"));
  assert(contains(saved, "\"restartRequired\":true"));
  assert(contains(saved, "\"display\":\"none\""));
  assert(contains(saved, "\"input\":\"none\""));
  assert(contains(saved, std::string("\"audio\":\"") +
                             runtimeAudioOutputWebValue(requested.audio) +
                             "\""));
  assert(formatRuntimeHardwareWebSaveJson(savedJson, sizeof(savedJson),
                                          requested, false));
  assert(contains(savedJson, "\"changed\":false"));

  // The web adapter only validates and persists a candidate. It neither
  // changes the build descriptor nor invokes display/input activation.
  assert(currentHardware().displayKind == hardware.displayKind);
  assert(defaultRuntimeHardwareConfig() == current);
}
