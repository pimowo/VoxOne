#include "../src/hardware/runtime_hardware_config.h"
#include "../src/hardware/runtime_audio_output.h"
#include "../profiles/profile.h"

#include <cassert>
#include <cstring>

using namespace voxone::display_profile;
using namespace voxone::hardware;

namespace {
class FakeStorage : public RuntimeHardwareStorage {
 public:
  RuntimeHardwareStorageReadStatus read(uint8_t* output, size_t capacity,
                                        size_t& outputSize) override {
    ++reads;
    if (readError) return RuntimeHardwareStorageReadStatus::Error;
    if (!present) return RuntimeHardwareStorageReadStatus::Missing;
    outputSize = size;
    if (size <= capacity) std::memcpy(output, bytes, size);
    return RuntimeHardwareStorageReadStatus::Found;
  }

  bool write(const uint8_t* input, size_t inputSize) override {
    ++writes;
    if (writeError || inputSize > sizeof(bytes)) return false;
    std::memcpy(bytes, input, inputSize);
    size = inputSize;
    present = true;
    return true;
  }

  uint8_t bytes[32]{};
  size_t size = 0;
  unsigned reads = 0;
  unsigned writes = 0;
  bool present = false;
  bool readError = false;
  bool writeError = false;
};

RuntimeDisplay unsupportedDisplay(const HardwareCapabilities& capabilities) {
  const RuntimeDisplay candidates[] = {
      RuntimeDisplay::Ssd1306_128x64, RuntimeDisplay::Ssd1309_128x64,
      RuntimeDisplay::St7789_284x76, RuntimeDisplay::St7796s_480x320,
      RuntimeDisplay::Ssd1306_128x32, RuntimeDisplay::Sh1106_128x64,
      RuntimeDisplay::Ch1116g_128x64};
  for (RuntimeDisplay candidate : candidates) {
    DisplayKind kind = DisplayKind::None;
    assert(runtimeDisplayKind(candidate, kind));
    if (!capabilities.supportsDisplay(kind)) return candidate;
  }
  assert(false);
  return RuntimeDisplay::None;
}

void putRecord(FakeStorage& storage, const RuntimeHardwareConfig& config) {
  assert(serializeRuntimeHardwareConfig(config, storage.bytes,
                                        kRuntimeHardwareConfigSerializedSize));
  storage.size = kRuntimeHardwareConfigSerializedSize;
  storage.present = true;
}
}  // namespace

int main() {
  const HardwareDescriptor& hardware = currentHardware();
  const HardwareCapabilities& capabilities = hardwareCapabilities();
  const RuntimeHardwareConfig defaults = defaultRuntimeHardwareConfig();
  static_assert(static_cast<uint8_t>(RuntimeDisplay::Ssd1306_128x64) == 1);
  static_assert(static_cast<uint8_t>(RuntimeDisplay::Ssd1309_128x64) == 2);
  static_assert(static_cast<uint8_t>(RuntimeDisplay::St7789_284x76) == 3);
  static_assert(static_cast<uint8_t>(RuntimeDisplay::St7796s_480x320) == 4);
  static_assert(static_cast<uint8_t>(RuntimeDisplay::Ssd1306_128x32) == 5);
  static_assert(static_cast<uint8_t>(RuntimeDisplay::Sh1106_128x64) == 6);
  static_assert(static_cast<uint8_t>(RuntimeDisplay::Ch1116g_128x64) == 7);
  assert(defaults.version == kRuntimeHardwareConfigVersion);
#if defined(VOXONE_PROFILE_S0)
  assert(defaults.audio == RuntimeAudioOutput::Max98357);
#else
  assert(defaults.audio == RuntimeAudioOutput::Pcm5102a);
#endif
  assert(validateRuntimeHardwareConfig(defaults, capabilities) ==
         RuntimeHardwareValidation::Valid);
  const RuntimeAudioActivationPlan defaultAudioPlan =
      runtimeAudioActivationPlan(defaults.audio);
  assert(defaultAudioPlan.initializeSerialI2s);
  assert(defaultAudioPlan.localPlaybackAvailable);
  const RuntimeAudioActivationPlan noneAudioPlan =
      runtimeAudioActivationPlan(RuntimeAudioOutput::None);
  assert(!noneAudioPlan.initializeSerialI2s);
  assert(!noneAudioPlan.localPlaybackAvailable);
  const RuntimeAudioActivationPlan dspAudioPlan =
      runtimeAudioActivationPlan(RuntimeAudioOutput::DspMini);
  assert(!dspAudioPlan.initializeSerialI2s);
  assert(!dspAudioPlan.localPlaybackAvailable);
  const RuntimeAudioActivationPlan stereoMaxAudioPlan =
      runtimeAudioActivationPlan(RuntimeAudioOutput::Max98357Stereo);
  assert(stereoMaxAudioPlan.initializeSerialI2s);
  assert(stereoMaxAudioPlan.localPlaybackAvailable);

#if defined(VOXONE_PROFILE_W0)
  assert(defaults.display == RuntimeDisplay::St7789_284x76);
  assert(defaults.input == RuntimeInput::Ec11);
#elif defined(VOXONE_PROFILE_S0)
  assert(defaults.display == RuntimeDisplay::Ssd1306_128x64);
  assert(defaults.input == RuntimeInput::Ec11);
#elif defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_DIN0)
  assert(defaults.display == RuntimeDisplay::Ssd1306_128x64);
  assert(defaults.input == RuntimeInput::Ec11);
#elif defined(VOXONE_PROFILE_L0)
  assert(defaults.display == RuntimeDisplay::St7796s_480x320);
  assert(defaults.input == RuntimeInput::Ec11);
#endif

  HardwareDescriptor noDisplay = hardware;
  noDisplay.displayKind = DisplayKind::None;
  noDisplay.display = {kNoPin, kNoPin, kNoPin, kNoPin, BusKind::None,
                       DisplayKind::None};
  noDisplay.localInput = {kNoPin, kNoPin, kNoPin, 1, false};
  noDisplay.capabilities.supportedDisplays = 0;
  noDisplay.capabilities.supportedInputs = 0;
  noDisplay.capabilities.supportsLocalUi = false;
  const RuntimeHardwareConfig noDisplayDefaults =
      defaultRuntimeHardwareConfig(noDisplay);
  assert(noDisplayDefaults.display == RuntimeDisplay::None);
  assert(noDisplayDefaults.input == RuntimeInput::None);
  assert(noDisplayDefaults.audio == defaults.audio);
  assert(validateRuntimeHardwareConfig(noDisplayDefaults,
                                       noDisplay.capabilities) ==
         RuntimeHardwareValidation::Valid);
  RuntimeHardwareConfig unavailableInput = noDisplayDefaults;
  unavailableInput.input = RuntimeInput::Ec11;
  assert(validateRuntimeHardwareConfig(unavailableInput,
                                       noDisplay.capabilities) ==
         RuntimeHardwareValidation::Unsupported);
  unavailableInput.input = RuntimeInput::Buttons3;
  assert(validateRuntimeHardwareConfig(unavailableInput,
                                       noDisplay.capabilities) ==
         RuntimeHardwareValidation::Unsupported);

  assert(runtimeDisplayProfile(RuntimeDisplay::Ssd1306_128x64) ==
         ProfileId::Oled128x64);
  assert(runtimeDisplayProfile(RuntimeDisplay::Ssd1309_128x64) ==
         ProfileId::Oled128x64);
  assert(runtimeDisplayProfile(RuntimeDisplay::Ssd1306_128x32) ==
         ProfileId::Oled128x32);
  assert(runtimeDisplayProfile(RuntimeDisplay::Sh1106_128x64) ==
         ProfileId::Oled128x64);
  assert(runtimeDisplayProfile(RuntimeDisplay::Ch1116g_128x64) ==
         ProfileId::Oled128x64);
  assert(runtimeDisplayController(RuntimeDisplay::Ssd1306_128x64) ==
         ControllerId::Ssd1306);
  assert(runtimeDisplayController(RuntimeDisplay::Ssd1309_128x64) ==
         ControllerId::Ssd1309);
  assert(runtimeDisplayController(RuntimeDisplay::Ssd1306_128x32) ==
         ControllerId::Ssd1306);
  assert(runtimeDisplayController(RuntimeDisplay::Sh1106_128x64) ==
         ControllerId::Sh1106);
  assert(runtimeDisplayController(RuntimeDisplay::Ch1116g_128x64) ==
         ControllerId::Ch1116g);
  assert(runtimeDisplayProfile(RuntimeDisplay::St7789_284x76) ==
         ProfileId::St7789_284x76);
  assert(runtimeDisplayProfile(RuntimeDisplay::St7796s_480x320) ==
         ProfileId::St7796_480x320);
  assert(runtimeDisplayController(RuntimeDisplay::St7789_284x76) ==
         ControllerId::St7789);
  assert(runtimeDisplayController(RuntimeDisplay::St7796s_480x320) ==
         ControllerId::St7796);

  RuntimeHardwareConfig sh1106Record = defaults;
  sh1106Record.display = RuntimeDisplay::Sh1106_128x64;
  uint8_t sh1106Bytes[kRuntimeHardwareConfigSerializedSize]{};
  assert(serializeRuntimeHardwareConfig(sh1106Record, sh1106Bytes,
                                        sizeof(sh1106Bytes)));
  assert(sh1106Bytes[3] == 6);
  RuntimeHardwareConfig decodedSh1106{};
  assert(deserializeRuntimeHardwareConfig(sh1106Bytes, sizeof(sh1106Bytes),
                                           decodedSh1106) ==
         RuntimeHardwareDecodeStatus::Valid);
  assert(decodedSh1106 == sh1106Record);
  assert(validateRuntimeHardwareConfig(sh1106Record, capabilities) ==
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0) || \
    defined(VOXONE_PROFILE_DIN0) || defined(VOXONE_PROFILE_L0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif

  RuntimeHardwareConfig ch1116gRecord = defaults;
  ch1116gRecord.display = RuntimeDisplay::Ch1116g_128x64;
  uint8_t ch1116gBytes[kRuntimeHardwareConfigSerializedSize]{};
  assert(serializeRuntimeHardwareConfig(ch1116gRecord, ch1116gBytes,
                                        sizeof(ch1116gBytes)));
  assert(ch1116gBytes[3] == 7);
  RuntimeHardwareConfig decodedCh1116g{};
  assert(deserializeRuntimeHardwareConfig(ch1116gBytes,
                                           sizeof(ch1116gBytes),
                                           decodedCh1116g) ==
         RuntimeHardwareDecodeStatus::Valid);
  assert(decodedCh1116g == ch1116gRecord);
  assert(validateRuntimeHardwareConfig(ch1116gRecord, capabilities) ==
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0) || \
    defined(VOXONE_PROFILE_DIN0) || defined(VOXONE_PROFILE_L0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif

  RuntimeHardwareConfig candidate = defaults;
  candidate.display = unsupportedDisplay(capabilities);
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
         RuntimeHardwareValidation::Unsupported);
  candidate = defaults;
  candidate.audio = RuntimeAudioOutput::DspMini;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
#if defined(VOXONE_PROFILE_L0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif
  candidate.audio = RuntimeAudioOutput::Max98357;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif
  candidate.audio = RuntimeAudioOutput::None;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0) || \
    defined(VOXONE_PROFILE_L0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif
  candidate.audio = RuntimeAudioOutput::Max98357Stereo;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0)
         RuntimeHardwareValidation::Valid);
#else
         RuntimeHardwareValidation::Unsupported);
#endif
  candidate = defaults;
  candidate.input = RuntimeInput::None;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
         RuntimeHardwareValidation::Valid);
  candidate.input = RuntimeInput::Ec11;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
         (capabilities.supportsInput(LocalInputKind::Ec11)
              ? RuntimeHardwareValidation::Valid
              : RuntimeHardwareValidation::Unsupported));
  candidate.input = RuntimeInput::Buttons3;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
         (capabilities.supportsInput(LocalInputKind::Buttons3)
              ? RuntimeHardwareValidation::Valid
              : RuntimeHardwareValidation::Unsupported));
  candidate = defaults;
  candidate.display = static_cast<RuntimeDisplay>(0xff);
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
         RuntimeHardwareValidation::Invalid);
  candidate = defaults;
  candidate.version = 0xff;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
         RuntimeHardwareValidation::Invalid);

  FakeStorage storage;
  RuntimeHardwareLoadResult loaded =
      loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::DefaultMissing);
  assert(loaded.config == defaults && storage.writes == 0);

  assert(saveRuntimeHardwareConfig(storage, defaults, defaults, capabilities) ==
         RuntimeHardwareSaveStatus::Unchanged);
  assert(storage.writes == 0);

  const unsigned writesBeforeInvalid = storage.writes;
  assert(saveRuntimeHardwareConfig(storage, defaults, candidate, capabilities) ==
         RuntimeHardwareSaveStatus::Invalid);
  assert(storage.writes == writesBeforeInvalid);

  RuntimeHardwareConfig alternate = defaults;
  if (capabilities.supportsInput(LocalInputKind::Buttons3))
    alternate.input = RuntimeInput::Buttons3;
  else
    alternate.audio = RuntimeAudioOutput::None;
  assert(validateRuntimeHardwareConfig(alternate, capabilities) ==
         RuntimeHardwareValidation::Valid);
  assert(saveRuntimeHardwareConfig(storage, defaults, alternate, capabilities) ==
         RuntimeHardwareSaveStatus::Saved);
  assert(storage.writes == 1);
  loaded = loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::Loaded);
  assert(loaded.config == alternate);

  // Existing value 3 remains the one-module MONO selection.
  RuntimeHardwareConfig legacyMax = defaults;
  legacyMax.audio = RuntimeAudioOutput::Max98357;
  uint8_t legacyMaxRecord[kRuntimeHardwareConfigSerializedSize]{};
  assert(serializeRuntimeHardwareConfig(legacyMax, legacyMaxRecord,
                                        sizeof(legacyMaxRecord)));
  assert(legacyMaxRecord[5] == 3);
  RuntimeHardwareConfig decodedLegacyMax{};
  assert(deserializeRuntimeHardwareConfig(legacyMaxRecord,
                                           sizeof(legacyMaxRecord),
                                           decodedLegacyMax) ==
         RuntimeHardwareDecodeStatus::Valid);
  assert(decodedLegacyMax.audio == RuntimeAudioOutput::Max98357);

  // Value 4 is distinct, persists through the unchanged 10-byte record and
  // reloads after restart only on Mx/Sx capabilities.
  RuntimeHardwareConfig stereoMax = defaults;
  stereoMax.audio = RuntimeAudioOutput::Max98357Stereo;
#if defined(VOXONE_PROFILE_M0) || defined(VOXONE_PROFILE_S0)
  FakeStorage stereoStorage;
  assert(saveRuntimeHardwareConfig(stereoStorage, defaults, stereoMax,
                                   capabilities) ==
         RuntimeHardwareSaveStatus::Saved);
  assert(stereoStorage.size == kRuntimeHardwareConfigSerializedSize);
  assert(stereoStorage.bytes[5] == 4);
  const RuntimeHardwareLoadResult loadedStereo =
      loadRuntimeHardwareConfig(stereoStorage, hardware);
  assert(loadedStereo.status == RuntimeHardwareLoadStatus::Loaded);
  assert(loadedStereo.config == stereoMax);
#else
  assert(validateRuntimeHardwareConfig(stereoMax, capabilities) ==
         RuntimeHardwareValidation::Unsupported);
#endif

  RuntimeHardwareConfig invalidInput = defaults;
  invalidInput.input = static_cast<RuntimeInput>(0xff);
  assert(effectiveRuntimeInput(invalidInput, hardware) == defaults.input);
  RuntimeHardwareConfig unsupportedInput = noDisplayDefaults;
  unsupportedInput.input = RuntimeInput::Buttons3;
  assert(effectiveRuntimeInput(unsupportedInput, noDisplay) ==
         RuntimeInput::None);

  candidate = defaults;
  candidate.display = unsupportedDisplay(capabilities);
  const unsigned writesBeforeUnsupported = storage.writes;
  assert(saveRuntimeHardwareConfig(storage, defaults, candidate, capabilities) ==
         RuntimeHardwareSaveStatus::Unsupported);
  assert(storage.writes == writesBeforeUnsupported);

  putRecord(storage, defaults);
  storage.bytes[2] = 99;
  loaded = loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::DefaultInvalidVersion);
  assert(loaded.config == defaults);

  putRecord(storage, defaults);
  storage.bytes[3] = 0xff;
  loaded = loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::DefaultInvalidSelection);
  assert(loaded.config == defaults);

  putRecord(storage, defaults);
  storage.bytes[kRuntimeHardwareConfigSerializedSize - 1] ^= 0x80;
  loaded = loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::DefaultCorrupt);
  assert(loaded.config == defaults);

  putRecord(storage, defaults);
  storage.size = kRuntimeHardwareConfigSerializedSize - 1;
  loaded = loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::DefaultCorrupt);
  assert(loaded.config == defaults);

  putRecord(storage, defaults);
  storage.size = kRuntimeHardwareConfigSerializedSize + 1;
  loaded = loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::DefaultCorrupt);
  assert(loaded.config == defaults);

  candidate = defaults;
  candidate.display = unsupportedDisplay(capabilities);
  putRecord(storage, candidate);
  loaded = loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::DefaultUnsupported);
  assert(loaded.validation == RuntimeHardwareValidation::Unsupported);
  assert(loaded.config == defaults);

#if VOXONE_MULTI_DISPLAY_RUNTIME
  RuntimeHardwareConfig boardSupportedAlternative = defaults;
  boardSupportedAlternative.display = RuntimeDisplay::Sh1106_128x64;
  putRecord(storage, boardSupportedAlternative);
  loaded = loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::Loaded);
  assert(loaded.config == boardSupportedAlternative);
  // HardwareDescriptor continues to describe the build default. The display
  // activation layer consumes loaded.config separately.
  assert(currentHardware().displayKind ==
#if defined(VOXONE_PROFILE_S0)
         DisplayKind::Ssd1306_128x64);
#elif defined(VOXONE_PROFILE_L0)
         DisplayKind::St7796_480x320);
#else
         DisplayKind::Ssd1306_128x64);
#endif
#endif

  storage.readError = true;
  loaded = loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::DefaultStorageError);
  assert(loaded.config == defaults);
  storage.readError = false;
  assert(storage.writes == writesBeforeUnsupported);

  storage.writeError = true;
  assert(saveRuntimeHardwareConfig(storage, defaults, alternate, capabilities) ==
         RuntimeHardwareSaveStatus::StorageError);
}
