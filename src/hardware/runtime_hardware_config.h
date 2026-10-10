#ifndef VOXONE_RUNTIME_HARDWARE_CONFIG_H
#define VOXONE_RUNTIME_HARDWARE_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "hardware_descriptor.h"
#include "../displays/display_profile.h"

namespace voxone {
namespace hardware {

constexpr uint8_t kRuntimeHardwareConfigVersion = 1;
constexpr uint16_t kRuntimeHardwareConfigMagic = 0x4857;
constexpr size_t kRuntimeHardwareConfigSerializedSize = 10;

enum class RuntimeDisplay : uint8_t {
  None = 0,
  Ssd1306_128x64 = 1,
  Ssd1309_128x64 = 2,
  St7789_284x76 = 3,
  St7796s_480x320 = 4
};

enum class RuntimeInput : uint8_t {
  None = 0,
  Ec11 = 1,
  Buttons3 = 2
};

enum class RuntimeAudioOutput : uint8_t {
  None = 0,
  Pcm5102a = 1,
  DspMini = 2,
  Max98357 = 3
};

struct RuntimeHardwareConfig {
  uint8_t version;
  RuntimeDisplay display;
  RuntimeInput input;
  RuntimeAudioOutput audio;
};

inline bool operator==(const RuntimeHardwareConfig& left,
                       const RuntimeHardwareConfig& right) {
  return left.version == right.version && left.display == right.display &&
         left.input == right.input && left.audio == right.audio;
}
inline bool operator!=(const RuntimeHardwareConfig& left,
                       const RuntimeHardwareConfig& right) {
  return !(left == right);
}

enum class RuntimeHardwareValidation : uint8_t {
  Valid,
  Invalid,
  Unsupported
};

enum class RuntimeHardwareDecodeStatus : uint8_t {
  Valid,
  InvalidSize,
  InvalidMagic,
  InvalidVersion,
  Corrupt,
  InvalidSelection
};

enum class RuntimeHardwareStorageReadStatus : uint8_t {
  Found,
  Missing,
  Error
};

enum class RuntimeHardwareLoadStatus : uint8_t {
  Loaded,
  DefaultMissing,
  DefaultInvalidVersion,
  DefaultCorrupt,
  DefaultInvalidSelection,
  DefaultUnsupported,
  DefaultStorageError
};

enum class RuntimeHardwareSaveStatus : uint8_t {
  Saved,
  Unchanged,
  Invalid,
  Unsupported,
  StorageError
};

class RuntimeHardwareStorage {
 public:
  virtual ~RuntimeHardwareStorage() {}
  virtual RuntimeHardwareStorageReadStatus read(uint8_t* output,
                                                 size_t capacity,
                                                 size_t& size) = 0;
  virtual bool write(const uint8_t* input, size_t size) = 0;
};

struct RuntimeHardwareLoadResult {
  RuntimeHardwareConfig config;
  RuntimeHardwareLoadStatus status;
  RuntimeHardwareValidation validation;
};

struct RuntimeHardwareDiagnostics {
  RuntimeHardwareLoadStatus loadStatus;
  RuntimeHardwareValidation validation;
};

RuntimeHardwareConfig defaultRuntimeHardwareConfig(
    const HardwareDescriptor& hardware);
RuntimeHardwareConfig defaultRuntimeHardwareConfig();

bool runtimeDisplayKind(RuntimeDisplay selection, DisplayKind& output);
bool runtimeAudioOutputKind(RuntimeAudioOutput selection,
                            AudioOutputKind& output);
display_profile::ProfileId runtimeDisplayProfile(RuntimeDisplay selection);
display_profile::ControllerId runtimeDisplayController(
    RuntimeDisplay selection);

RuntimeHardwareValidation validateRuntimeHardwareConfig(
    const RuntimeHardwareConfig& config,
    const HardwareCapabilities& capabilities);

uint32_t runtimeHardwareConfigCrc32(const uint8_t* data, size_t size);
bool serializeRuntimeHardwareConfig(const RuntimeHardwareConfig& config,
                                    uint8_t* output, size_t outputSize);
RuntimeHardwareDecodeStatus deserializeRuntimeHardwareConfig(
    const uint8_t* input, size_t inputSize, RuntimeHardwareConfig& output);

RuntimeHardwareLoadResult loadRuntimeHardwareConfig(
    RuntimeHardwareStorage& storage, const HardwareDescriptor& hardware);
RuntimeHardwareSaveStatus saveRuntimeHardwareConfig(
    RuntimeHardwareStorage& storage, const RuntimeHardwareConfig& current,
    const RuntimeHardwareConfig& requested,
    const HardwareCapabilities& capabilities);

// Device API. The selected values are persisted and diagnosed, but the
// current build-time display/input/audio drivers remain authoritative until
// the later one-board-one-firmware activation stage.
const RuntimeHardwareConfig& runtimeHardwareConfig();
RuntimeHardwareLoadStatus loadRuntimeHardwareConfig();
RuntimeHardwareSaveStatus saveRuntimeHardwareConfig(
    const RuntimeHardwareConfig& requested);
RuntimeHardwareDiagnostics runtimeHardwareDiagnostics();
const char* runtimeHardwareLoadStatusName(RuntimeHardwareLoadStatus status);

}  // namespace hardware
}  // namespace voxone

#endif
