#include "../src/hardware/runtime_hardware_config.h"
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
      RuntimeDisplay::St7789_284x76, RuntimeDisplay::St7796s_480x320};
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
  assert(defaults.version == kRuntimeHardwareConfigVersion);
  assert(defaults.audio == RuntimeAudioOutput::Pcm5102a);
  assert(validateRuntimeHardwareConfig(defaults, capabilities) ==
         RuntimeHardwareValidation::Valid);

#if defined(VOXONE_PROFILE_X0)
  assert(defaults.display == RuntimeDisplay::St7789_284x76);
  assert(defaults.input == RuntimeInput::Ec11);
#elif defined(VOXONE_PROFILE_B0)
  assert(defaults.display == RuntimeDisplay::None);
  assert(defaults.input == RuntimeInput::None);
#elif defined(VOXONE_PROFILE_C0)
#if defined(VOXONE_C0_DISPLAY_SSD1309)
  assert(defaults.display == RuntimeDisplay::Ssd1309_128x64);
#else
  assert(defaults.display == RuntimeDisplay::Ssd1306_128x64);
#endif
  assert(defaults.input == RuntimeInput::Ec11);
#elif defined(VOXONE_PROFILE_A0)
  assert(defaults.display == RuntimeDisplay::St7796s_480x320);
  assert(defaults.input == RuntimeInput::Ec11);
#endif

  assert(runtimeDisplayProfile(RuntimeDisplay::Ssd1306_128x64) ==
         ProfileId::Oled128x64);
  assert(runtimeDisplayProfile(RuntimeDisplay::Ssd1309_128x64) ==
         ProfileId::Oled128x64);
  assert(runtimeDisplayController(RuntimeDisplay::Ssd1306_128x64) ==
         ControllerId::Ssd1306);
  assert(runtimeDisplayController(RuntimeDisplay::Ssd1309_128x64) ==
         ControllerId::Ssd1309);
  assert(runtimeDisplayProfile(RuntimeDisplay::St7789_284x76) ==
         ProfileId::St7789_284x76);
  assert(runtimeDisplayProfile(RuntimeDisplay::St7796s_480x320) ==
         ProfileId::St7796_480x320);
  assert(runtimeDisplayController(RuntimeDisplay::St7789_284x76) ==
         ControllerId::St7789);
  assert(runtimeDisplayController(RuntimeDisplay::St7796s_480x320) ==
         ControllerId::St7796);

  RuntimeHardwareConfig candidate = defaults;
  candidate.display = unsupportedDisplay(capabilities);
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
         RuntimeHardwareValidation::Unsupported);
  candidate = defaults;
  candidate.audio = RuntimeAudioOutput::DspMini;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
         RuntimeHardwareValidation::Unsupported);
  candidate.audio = RuntimeAudioOutput::Max98357;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
         RuntimeHardwareValidation::Unsupported);
  candidate = defaults;
  candidate.input = RuntimeInput::Ec11;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
         (capabilities.supportsEncoder ? RuntimeHardwareValidation::Valid
                                      : RuntimeHardwareValidation::Unsupported));
  candidate.input = RuntimeInput::Buttons3;
  assert(validateRuntimeHardwareConfig(candidate, capabilities) ==
         (capabilities.supportsEncoder && capabilities.supportsLocalUi
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
  if (capabilities.supportsEncoder)
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

#if defined(VOXONE_PROFILE_C0) && !defined(VOXONE_C0_DISPLAY_SSD1309)
  RuntimeHardwareConfig boardSupportedButNotBuilt = defaults;
  boardSupportedButNotBuilt.display = RuntimeDisplay::Ssd1309_128x64;
  putRecord(storage, boardSupportedButNotBuilt);
  loaded = loadRuntimeHardwareConfig(storage, hardware);
  assert(loaded.status == RuntimeHardwareLoadStatus::Loaded);
  assert(loaded.config == boardSupportedButNotBuilt);
  assert(currentHardware().displayKind == DisplayKind::Ssd1306_128x64);
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
