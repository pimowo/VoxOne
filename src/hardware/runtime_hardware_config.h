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
  St7796s_480x320 = 4,
  // Appended so existing Config v7 hardware records retain their meaning.
  Ssd1306_128x32 = 5,
  // Appended without changing the Config v7 record layout.
  Sh1106_128x64 = 6
};

static_assert(static_cast<uint8_t>(RuntimeDisplay::Ssd1306_128x32) == 5,
              "existing OLED display identities must remain stable");
static_assert(static_cast<uint8_t>(RuntimeDisplay::Sh1106_128x64) == 6,
              "SH1106 requires a distinct persisted identity");

enum class RuntimeInput : uint8_t {
  None = 0,
  Ec11 = 1,
  Buttons3 = 2
};

enum class RuntimeAudioOutput : uint8_t {
  None = 0,
  Pcm5102a = 1,
  DspMini = 2,
  // Value 3 is the pre-existing single-module (MONO) selection.
  Max98357 = 3,
  // Appended without changing Config v7 record version, size or offsets.
  Max98357Stereo = 4
};

static_assert(static_cast<uint8_t>(RuntimeAudioOutput::Max98357) == 3,
              "existing MAX98357A Config identity must remain MONO");
static_assert(static_cast<uint8_t>(RuntimeAudioOutput::Max98357Stereo) == 4,
              "stereo MAX98357A must have a distinct persisted identity");

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
bool runtimeInputKind(RuntimeInput selection, LocalInputKind& output);
bool runtimeAudioOutputKind(RuntimeAudioOutput selection,
                            AudioOutputKind& output);
RuntimeInput effectiveRuntimeInput(const RuntimeHardwareConfig& config,
                                   const HardwareDescriptor& hardware);
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

// Device API. M0/S0/DIN0 activate a selected compiled OLED backend/layout.
// Every current board activates the selected local input and audio backend at
// startup. A saved audio change takes effect after the required restart.
const RuntimeHardwareConfig& runtimeHardwareConfig();
RuntimeHardwareLoadStatus loadRuntimeHardwareConfig();
RuntimeHardwareSaveStatus saveRuntimeHardwareConfig(
    const RuntimeHardwareConfig& requested);
bool readRuntimeHardwarePersistent(RuntimeHardwareConfig& config, bool& found);
bool writeRuntimeHardwarePersistent(const RuntimeHardwareConfig& config);
bool eraseRuntimeHardwarePersistent();
RuntimeHardwareDiagnostics runtimeHardwareDiagnostics();
const char* runtimeHardwareLoadStatusName(RuntimeHardwareLoadStatus status);

}  // namespace hardware
}  // namespace voxone

#endif
