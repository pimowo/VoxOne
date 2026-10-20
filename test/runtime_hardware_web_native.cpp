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

  RuntimeHardwareWebOption displays[5]{};
  const size_t displayCount =
      supportedRuntimeDisplayOptions(capabilities, displays, 5);
  assert(displayCount == 2 || displayCount == 3);
  assert(std::strcmp(displays[0].value, "none") == 0);
  assert(std::strcmp(displays[0].label, "Brak") == 0);
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0) || \
    defined(VOXONE_PROFILE_DIN0)
  assert(displayCount == 3);
  assert(hasOption(displays, displayCount, "ssd1306_128x64"));
  assert(hasOption(displays, displayCount, "ssd1309_128x64"));
  assert(!hasOption(displays, displayCount, "st7796s_480x320"));
#elif defined(VOXONE_PROFILE_L0)
  assert(displayCount == 2);
  assert(hasOption(displays, displayCount, "st7796s_480x320"));
  assert(!hasOption(displays, displayCount, "ssd1306_128x64"));
#elif defined(VOXONE_PROFILE_W0)
  assert(displayCount == 2);
  assert(hasOption(displays, displayCount, "st7789_284x76"));
  assert(!hasOption(displays, displayCount, "ssd1306_128x64"));
#endif

  RuntimeHardwareWebOption inputs[3]{};
  const size_t inputCount = supportedRuntimeInputOptions(capabilities, inputs, 3);
  assert(inputCount == 3);
  assert(std::strcmp(inputs[0].value, "none") == 0);
  assert(std::strcmp(inputs[0].label, "Brak") == 0);
  assert(hasOption(inputs, inputCount, "ec11"));
  assert(hasOption(inputs, inputCount, "buttons3"));

  RuntimeHardwareWebOption audioOutputs[4]{};
  const size_t audioOutputCount =
      supportedRuntimeAudioOutputOptions(capabilities, audioOutputs, 4);
  assert(audioOutputCount == 1);
  assert(std::strcmp(audioOutputs[0].value, "pcm5102a") == 0);
  assert(std::strcmp(audioOutputs[0].label, "PCM5102A") == 0);
  assert(!hasOption(audioOutputs, audioOutputCount, "none"));
  assert(!hasOption(audioOutputs, audioOutputCount, "max98357a"));
  assert(!hasOption(audioOutputs, audioOutputCount, "dspmini"));

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
      "none", "ssd1306_128x64", "ssd1309_128x64", "ch1116_128x64",
      "sh1106_128x64", "st7290_128x64_tbd", "st7789v_320x240",
      "ssd1322_256x64", "st7796s_480x320", "gc9a01_240x240"};
#elif defined(VOXONE_PROFILE_S0) || defined(VOXONE_PROFILE_DIN0)
  const char* const expectedDisplays[] = {
      "none", "ssd1306_128x32", "ssd1306_128x64"};
#elif defined(VOXONE_PROFILE_W0)
  const char* const expectedDisplays[] = {
      "none", "ssd1306_128x64", "ssd1309_128x64", "ch1116_128x64",
      "sh1106_128x64", "st7789v_320x240", "st7789_284x76"};
#endif
  assertRoadmapOrder(displayRoadmap, displayRoadmapCount, expectedDisplays,
                     sizeof(expectedDisplays) / sizeof(expectedDisplays[0]));
  const HardwareRoadmapItem* noneDisplay =
      findRoadmapItem(displayRoadmap, displayRoadmapCount, "none");
  assert(noneDisplay && noneDisplay->selectable);
  assert(noneDisplay->status == HardwareRoadmapStatus::Supported);
  const HardwareRoadmapItem* plannedDisplay = findRoadmapItem(
      displayRoadmap, displayRoadmapCount,
#if defined(VOXONE_PROFILE_S0) || defined(VOXONE_PROFILE_DIN0)
      "ssd1306_128x32"
#else
      "ch1116_128x64"
#endif
  );
  assert(plannedDisplay && !plannedDisplay->selectable);
  assert(plannedDisplay->status == HardwareRoadmapStatus::Planned);
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
  HardwareRoadmapItem audioRoadmap[4]{};
  HardwareRoadmapItem moduleRoadmap[4]{};
  const size_t audioRoadmapCount =
      hardwareAudioRoadmap(hardware, audioRoadmap, 4);
  const size_t moduleRoadmapCount =
      hardwareModuleRoadmap(hardware, moduleRoadmap, 4);
#if defined(VOXONE_PROFILE_L0)
  const char* const expectedAudio[] = {"pcm5102a", "dspmini"};
  const char* const expectedModules[] = {"voxonebt", "ds3231", "w5500"};
#elif defined(VOXONE_PROFILE_M0)
  const char* const expectedAudio[] = {"pcm5102a", "max98357a"};
  const char* const expectedModules[] = {"voxonebt", "ds3231", "battery"};
#elif defined(VOXONE_PROFILE_S0)
  const char* const expectedAudio[] = {"pcm5102a", "max98357a"};
  const char* const expectedModules[] = {"voxonebt", "ds3231", "battery"};
#elif defined(VOXONE_PROFILE_DIN0)
  const char* const expectedAudio[] = {"pcm5102a"};
  const char* const expectedModules[] = {"voxonebt"};
#elif defined(VOXONE_PROFILE_W0)
  const char* const expectedAudio[] = {"pcm5102a"};
  const char* const expectedModules[] = {"voxonebt"};
#endif
  assertRoadmapOrder(audioRoadmap, audioRoadmapCount, expectedAudio,
                     sizeof(expectedAudio) / sizeof(expectedAudio[0]));
  assertRoadmapOrder(moduleRoadmap, moduleRoadmapCount, expectedModules,
                     sizeof(expectedModules) / sizeof(expectedModules[0]));
  for (size_t index = 0; index < audioRoadmapCount; ++index)
    assert(!audioRoadmap[index].selectable);
  for (size_t index = 0; index < moduleRoadmapCount; ++index)
    assert(!moduleRoadmap[index].selectable);
#if defined(VOXONE_PROFILE_L0) || defined(VOXONE_PROFILE_M0)
  assert(findRoadmapItem(audioRoadmap, audioRoadmapCount,
                         "pcm5102a")->current);
#endif
#if defined(VOXONE_PROFILE_S0)
  assert(findRoadmapItem(audioRoadmap, audioRoadmapCount,
                         "pcm5102a")->current);
  assert(std::strcmp(audioRoadmap[0].note, "Bieżący prototyp") == 0);
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
             current, capabilities, runtimeDisplayWebValue(current.display),
             "ec11", "none", candidate) ==
         RuntimeHardwareValidation::Unsupported);
  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, runtimeDisplayWebValue(current.display),
             "ec11", "bad", candidate) ==
         RuntimeHardwareValidation::Invalid);
  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, runtimeDisplayWebValue(current.display),
             "ec11", "max98357a", candidate) ==
         RuntimeHardwareValidation::Unsupported);
  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, runtimeDisplayWebValue(current.display),
             "ec11", "dspmini", candidate) ==
         RuntimeHardwareValidation::Unsupported);

  assert(makeRuntimeHardwareWebCandidate(
             current, capabilities, runtimeDisplayWebValue(current.display),
             "buttons3", candidate) == RuntimeHardwareValidation::Valid);
  assert(candidate.display == current.display);
  assert(candidate.input == RuntimeInput::Buttons3);

  assert(makeRuntimeHardwareWebCandidate(current, capabilities, "bad", "ec11",
                                         candidate) ==
         RuntimeHardwareValidation::Invalid);
  assert(makeRuntimeHardwareWebCandidate(current, capabilities,
                                         plannedDisplay->value, "ec11",
                                         candidate) ==
         RuntimeHardwareValidation::Invalid);
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
  for (const char* value : {"ssd1306_128x64", "ssd1309_128x64",
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
  assert(contains(saved, "\"audio\":\"pcm5102a\""));
  assert(formatRuntimeHardwareWebSaveJson(savedJson, sizeof(savedJson),
                                          requested, false));
  assert(contains(savedJson, "\"changed\":false"));

  // The web adapter only validates and persists a candidate. It neither
  // changes the build descriptor nor invokes display/input activation.
  assert(currentHardware().displayKind == hardware.displayKind);
  assert(defaultRuntimeHardwareConfig() == current);
}
