#include "runtime_hardware_config.h"

#include <string.h>

namespace voxone {
namespace hardware {
namespace {
constexpr size_t kMagicOffset = 0;
constexpr size_t kVersionOffset = 2;
constexpr size_t kDisplayOffset = 3;
constexpr size_t kInputOffset = 4;
constexpr size_t kAudioOffset = 5;
constexpr size_t kCrcOffset = 6;

bool validRuntimeDisplay(RuntimeDisplay value) {
  switch (value) {
    case RuntimeDisplay::None:
    case RuntimeDisplay::Ssd1306_128x64:
    case RuntimeDisplay::Ssd1309_128x64:
    case RuntimeDisplay::St7789_284x76:
    case RuntimeDisplay::St7796s_480x320:
      return true;
  }
  return false;
}

bool validRuntimeInput(RuntimeInput value) {
  switch (value) {
    case RuntimeInput::None:
    case RuntimeInput::Ec11:
    case RuntimeInput::Buttons3:
      return true;
  }
  return false;
}

bool validRuntimeAudio(RuntimeAudioOutput value) {
  switch (value) {
    case RuntimeAudioOutput::None:
    case RuntimeAudioOutput::Pcm5102a:
    case RuntimeAudioOutput::DspMini:
    case RuntimeAudioOutput::Max98357:
      return true;
  }
  return false;
}

RuntimeDisplay runtimeDisplayFromKind(DisplayKind kind) {
  switch (kind) {
    case DisplayKind::None: return RuntimeDisplay::None;
    case DisplayKind::Ssd1306_128x64: return RuntimeDisplay::Ssd1306_128x64;
    case DisplayKind::Ssd1309_128x64: return RuntimeDisplay::Ssd1309_128x64;
    case DisplayKind::St7789_284x76: return RuntimeDisplay::St7789_284x76;
    case DisplayKind::St7796_480x320: return RuntimeDisplay::St7796s_480x320;
  }
  return RuntimeDisplay::None;
}

RuntimeAudioOutput runtimeAudioFromKind(AudioOutputKind kind) {
  switch (kind) {
    case AudioOutputKind::None: return RuntimeAudioOutput::None;
    case AudioOutputKind::Pcm5102a: return RuntimeAudioOutput::Pcm5102a;
    case AudioOutputKind::DspMini: return RuntimeAudioOutput::DspMini;
    case AudioOutputKind::Max98357: return RuntimeAudioOutput::Max98357;
  }
  return RuntimeAudioOutput::None;
}

void write16(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
}

void write32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

uint16_t read16(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) |
         static_cast<uint16_t>(input[1]) << 8;
}

uint32_t read32(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) |
         static_cast<uint32_t>(input[1]) << 8 |
         static_cast<uint32_t>(input[2]) << 16 |
         static_cast<uint32_t>(input[3]) << 24;
}

RuntimeHardwareLoadStatus loadStatusForDecode(
    RuntimeHardwareDecodeStatus status) {
  switch (status) {
    case RuntimeHardwareDecodeStatus::InvalidVersion:
      return RuntimeHardwareLoadStatus::DefaultInvalidVersion;
    case RuntimeHardwareDecodeStatus::InvalidSelection:
      return RuntimeHardwareLoadStatus::DefaultInvalidSelection;
    case RuntimeHardwareDecodeStatus::Valid:
      return RuntimeHardwareLoadStatus::Loaded;
    case RuntimeHardwareDecodeStatus::InvalidSize:
    case RuntimeHardwareDecodeStatus::InvalidMagic:
    case RuntimeHardwareDecodeStatus::Corrupt:
      return RuntimeHardwareLoadStatus::DefaultCorrupt;
  }
  return RuntimeHardwareLoadStatus::DefaultCorrupt;
}
}  // namespace

RuntimeHardwareConfig defaultRuntimeHardwareConfig(
    const HardwareDescriptor& hardware) {
  return RuntimeHardwareConfig{
      kRuntimeHardwareConfigVersion,
      runtimeDisplayFromKind(hardware.displayKind),
      hardware.capabilities.supportsEncoder ? RuntimeInput::Ec11
                                            : RuntimeInput::None,
      runtimeAudioFromKind(hardware.audioOutputKind)};
}

RuntimeHardwareConfig defaultRuntimeHardwareConfig() {
  return defaultRuntimeHardwareConfig(currentHardware());
}

bool runtimeDisplayKind(RuntimeDisplay selection, DisplayKind& output) {
  switch (selection) {
    case RuntimeDisplay::None: output = DisplayKind::None; return true;
    case RuntimeDisplay::Ssd1306_128x64:
      output = DisplayKind::Ssd1306_128x64; return true;
    case RuntimeDisplay::Ssd1309_128x64:
      output = DisplayKind::Ssd1309_128x64; return true;
    case RuntimeDisplay::St7789_284x76:
      output = DisplayKind::St7789_284x76; return true;
    case RuntimeDisplay::St7796s_480x320:
      output = DisplayKind::St7796_480x320; return true;
  }
  return false;
}

bool runtimeAudioOutputKind(RuntimeAudioOutput selection,
                            AudioOutputKind& output) {
  switch (selection) {
    case RuntimeAudioOutput::None: output = AudioOutputKind::None; return true;
    case RuntimeAudioOutput::Pcm5102a:
      output = AudioOutputKind::Pcm5102a; return true;
    case RuntimeAudioOutput::DspMini:
      output = AudioOutputKind::DspMini; return true;
    case RuntimeAudioOutput::Max98357:
      output = AudioOutputKind::Max98357; return true;
  }
  return false;
}

display_profile::ProfileId runtimeDisplayProfile(RuntimeDisplay selection) {
  DisplayKind kind = DisplayKind::None;
  if (!runtimeDisplayKind(selection, kind)) return display_profile::ProfileId::None;
  return display_profile::profileFor(kind);
}

display_profile::ControllerId runtimeDisplayController(
    RuntimeDisplay selection) {
  DisplayKind kind = DisplayKind::None;
  if (!runtimeDisplayKind(selection, kind))
    return display_profile::ControllerId::None;
  return display_profile::controllerFor(kind);
}

RuntimeHardwareValidation validateRuntimeHardwareConfig(
    const RuntimeHardwareConfig& config,
    const HardwareCapabilities& capabilities) {
  if (config.version != kRuntimeHardwareConfigVersion ||
      !validRuntimeDisplay(config.display) ||
      !validRuntimeInput(config.input) ||
      !validRuntimeAudio(config.audio))
    return RuntimeHardwareValidation::Invalid;

  DisplayKind display = DisplayKind::None;
  AudioOutputKind audio = AudioOutputKind::None;
  if (!runtimeDisplayKind(config.display, display) ||
      !runtimeAudioOutputKind(config.audio, audio))
    return RuntimeHardwareValidation::Invalid;

  if ((display != DisplayKind::None &&
       !capabilities.supportsDisplay(display)) ||
      (audio != AudioOutputKind::None &&
       !capabilities.supportsAudioOutput(audio)))
    return RuntimeHardwareValidation::Unsupported;

  if (config.input == RuntimeInput::Ec11 && !capabilities.supportsEncoder)
    return RuntimeHardwareValidation::Unsupported;
  // Buttons3 will reuse the same A/B/KEY local-input lines. This validates
  // physical board support only; no Buttons3 runtime driver is activated yet.
  if (config.input == RuntimeInput::Buttons3 &&
      (!capabilities.supportsLocalUi || !capabilities.supportsEncoder))
    return RuntimeHardwareValidation::Unsupported;

  return RuntimeHardwareValidation::Valid;
}

uint32_t runtimeHardwareConfigCrc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc ^ 0xffffffffu;
}

bool serializeRuntimeHardwareConfig(const RuntimeHardwareConfig& config,
                                    uint8_t* output, size_t outputSize) {
  if (!output || outputSize != kRuntimeHardwareConfigSerializedSize ||
      config.version != kRuntimeHardwareConfigVersion ||
      !validRuntimeDisplay(config.display) ||
      !validRuntimeInput(config.input) || !validRuntimeAudio(config.audio))
    return false;

  uint8_t record[kRuntimeHardwareConfigSerializedSize]{};
  write16(record + kMagicOffset, kRuntimeHardwareConfigMagic);
  record[kVersionOffset] = config.version;
  record[kDisplayOffset] = static_cast<uint8_t>(config.display);
  record[kInputOffset] = static_cast<uint8_t>(config.input);
  record[kAudioOffset] = static_cast<uint8_t>(config.audio);
  write32(record + kCrcOffset,
          runtimeHardwareConfigCrc32(record, kCrcOffset));
  memcpy(output, record, sizeof(record));
  return true;
}

RuntimeHardwareDecodeStatus deserializeRuntimeHardwareConfig(
    const uint8_t* input, size_t inputSize, RuntimeHardwareConfig& output) {
  if (!input || inputSize != kRuntimeHardwareConfigSerializedSize)
    return RuntimeHardwareDecodeStatus::InvalidSize;
  if (read16(input + kMagicOffset) != kRuntimeHardwareConfigMagic)
    return RuntimeHardwareDecodeStatus::InvalidMagic;
  if (input[kVersionOffset] != kRuntimeHardwareConfigVersion)
    return RuntimeHardwareDecodeStatus::InvalidVersion;

  const RuntimeHardwareConfig decoded{
      input[kVersionOffset], static_cast<RuntimeDisplay>(input[kDisplayOffset]),
      static_cast<RuntimeInput>(input[kInputOffset]),
      static_cast<RuntimeAudioOutput>(input[kAudioOffset])};
  if (!validRuntimeDisplay(decoded.display) ||
      !validRuntimeInput(decoded.input) || !validRuntimeAudio(decoded.audio))
    return RuntimeHardwareDecodeStatus::InvalidSelection;
  if (read32(input + kCrcOffset) !=
      runtimeHardwareConfigCrc32(input, kCrcOffset))
    return RuntimeHardwareDecodeStatus::Corrupt;
  output = decoded;
  return RuntimeHardwareDecodeStatus::Valid;
}

RuntimeHardwareLoadResult loadRuntimeHardwareConfig(
    RuntimeHardwareStorage& storage, const HardwareDescriptor& hardware) {
  RuntimeHardwareLoadResult result{
      defaultRuntimeHardwareConfig(hardware),
      RuntimeHardwareLoadStatus::DefaultMissing,
      RuntimeHardwareValidation::Valid};
  uint8_t record[kRuntimeHardwareConfigSerializedSize]{};
  size_t size = 0;
  const RuntimeHardwareStorageReadStatus read =
      storage.read(record, sizeof(record), size);
  if (read == RuntimeHardwareStorageReadStatus::Missing) return result;
  if (read == RuntimeHardwareStorageReadStatus::Error) {
    result.status = RuntimeHardwareLoadStatus::DefaultStorageError;
    return result;
  }

  RuntimeHardwareConfig stored = result.config;
  const RuntimeHardwareDecodeStatus decoded =
      deserializeRuntimeHardwareConfig(record, size, stored);
  if (decoded != RuntimeHardwareDecodeStatus::Valid) {
    result.status = loadStatusForDecode(decoded);
    result.validation = RuntimeHardwareValidation::Invalid;
    return result;
  }

  result.validation =
      validateRuntimeHardwareConfig(stored, hardware.capabilities);
  if (result.validation == RuntimeHardwareValidation::Unsupported) {
    result.status = RuntimeHardwareLoadStatus::DefaultUnsupported;
    return result;
  }
  if (result.validation != RuntimeHardwareValidation::Valid) {
    result.status = RuntimeHardwareLoadStatus::DefaultInvalidSelection;
    return result;
  }
  result.config = stored;
  result.status = RuntimeHardwareLoadStatus::Loaded;
  return result;
}

RuntimeHardwareSaveStatus saveRuntimeHardwareConfig(
    RuntimeHardwareStorage& storage, const RuntimeHardwareConfig& current,
    const RuntimeHardwareConfig& requested,
    const HardwareCapabilities& capabilities) {
  const RuntimeHardwareValidation validation =
      validateRuntimeHardwareConfig(requested, capabilities);
  if (validation == RuntimeHardwareValidation::Invalid)
    return RuntimeHardwareSaveStatus::Invalid;
  if (validation == RuntimeHardwareValidation::Unsupported)
    return RuntimeHardwareSaveStatus::Unsupported;
  if (requested == current) return RuntimeHardwareSaveStatus::Unchanged;

  uint8_t record[kRuntimeHardwareConfigSerializedSize];
  if (!serializeRuntimeHardwareConfig(requested, record, sizeof(record)))
    return RuntimeHardwareSaveStatus::Invalid;
  return storage.write(record, sizeof(record))
             ? RuntimeHardwareSaveStatus::Saved
             : RuntimeHardwareSaveStatus::StorageError;
}

}  // namespace hardware
}  // namespace voxone
