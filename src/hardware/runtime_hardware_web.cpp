#include "runtime_hardware_web.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace voxone {
namespace hardware {
namespace {

constexpr RuntimeDisplay kDisplays[] = {
    RuntimeDisplay::None, RuntimeDisplay::Ssd1306_128x64,
    RuntimeDisplay::Ssd1309_128x64, RuntimeDisplay::St7789_284x76,
    RuntimeDisplay::St7796s_480x320};
constexpr RuntimeInput kInputs[] = {
    RuntimeInput::None, RuntimeInput::Ec11, RuntimeInput::Buttons3};
constexpr RuntimeAudioOutput kAudioOutputs[] = {
    RuntimeAudioOutput::None, RuntimeAudioOutput::Pcm5102a,
    RuntimeAudioOutput::DspMini, RuntimeAudioOutput::Max98357};

struct CatalogItem {
  const char* value;
  const char* label;
  const char* note;
};

constexpr CatalogItem kLargeDisplays[] = {
    {"none", "Brak", ""},
    {"ssd1306_128x64", "SSD1306_128X64", ""},
    {"ssd1309_128x64", "SSD1309_128X64", ""},
    {"ch1116_128x64", "CH1116_128X64", ""},
    {"sh1106_128x64", "SH1106_128X64", ""},
    {"st7290_128x64_tbd", "ST7290_128X64 (?)",
     "Kontroler do potwierdzenia"},
    {"st7789v_320x240", "ST7789V_320X240", ""},
    {"ssd1322_256x64", "SSD1322_256X64", ""},
    {"st7796s_480x320", "ST7796S_480X320", ""},
    {"gc9a01_240x240", "GC9A01_240X240", ""}};
constexpr CatalogItem kSmallDisplays[] = {
    {"none", "Brak", ""},
    {"ssd1306_128x32", "SSD1306_128X32", ""},
    {"ssd1306_128x64", "SSD1306_128X64", ""}};
constexpr CatalogItem kWDisplays[] = {
    {"none", "Brak", ""},
    {"ssd1306_128x64", "SSD1306_128X64", ""},
    {"ssd1309_128x64", "SSD1309_128X64", ""},
    {"ch1116_128x64", "CH1116_128X64", ""},
    {"sh1106_128x64", "SH1106_128X64", ""},
    {"st7789v_320x240", "ST7789V_320X240", ""},
    {"st7789_284x76", "ST7789_284X76", ""}};
constexpr CatalogItem kInputCatalog[] = {
    {"none", "Brak", ""}, {"ec11", "EC11", ""},
    {"buttons3", "BUTTONS3", "Brak fizycznego PASS"}};

constexpr CatalogItem kLAudio[] = {
    {"pcm5102a", "PCM5102A", ""}, {"dspmini", "DSPmini", ""}};
constexpr CatalogItem kMAudio[] = {
    {"pcm5102a", "PCM5102A", ""}, {"max98357a", "MAX98357A", ""}};
constexpr CatalogItem kSAudio[] = {
    {"max98357a", "MAX98357A", ""}};
constexpr CatalogItem kDINAudio[] = {{"pcm5102a", "PCM5102A", ""}};
constexpr CatalogItem kWAudio[] = {{"pcm5102a", "PCM5102A", ""}};

constexpr CatalogItem kLModules[] = {
    {"voxonebt", "VoxOneBT", ""}, {"ds3231", "DS3231", ""},
    {"w5500", "W5500", ""}};
constexpr CatalogItem kMSModules[] = {
    {"voxonebt", "VoxOneBT", ""}, {"ds3231", "DS3231", ""},
    {"battery", "Pomiar baterii", ""}};
constexpr CatalogItem kDINModules[] = {{"voxonebt", "VoxOneBT", ""}};
constexpr CatalogItem kWModules[] = {{"voxonebt", "VoxOneBT", ""}};

template <size_t N>
void catalog(const CatalogItem (&items)[N], const CatalogItem*& output,
             size_t& count) {
  output = items;
  count = N;
}

void displayCatalog(BoardId board, const CatalogItem*& output, size_t& count) {
  switch (board) {
    case BoardId::BoardL0:
    case BoardId::BoardM0: catalog(kLargeDisplays, output, count); return;
    case BoardId::BoardS0:
    case BoardId::BoardDIN0: catalog(kSmallDisplays, output, count); return;
    case BoardId::BoardW0: catalog(kWDisplays, output, count); return;
  }
}

void audioCatalog(BoardId board, const CatalogItem*& output, size_t& count) {
  switch (board) {
    case BoardId::BoardL0: catalog(kLAudio, output, count); return;
    case BoardId::BoardM0: catalog(kMAudio, output, count); return;
    case BoardId::BoardS0: catalog(kSAudio, output, count); return;
    case BoardId::BoardDIN0: catalog(kDINAudio, output, count); return;
    case BoardId::BoardW0: catalog(kWAudio, output, count); return;
  }
}

void moduleCatalog(BoardId board, const CatalogItem*& output, size_t& count) {
  switch (board) {
    case BoardId::BoardL0: catalog(kLModules, output, count); return;
    case BoardId::BoardM0:
    case BoardId::BoardS0: catalog(kMSModules, output, count); return;
    case BoardId::BoardDIN0: catalog(kDINModules, output, count); return;
    case BoardId::BoardW0: catalog(kWModules, output, count); return;
  }
}

bool displayKindForValue(const char* value, DisplayKind& kind) {
  RuntimeDisplay display = RuntimeDisplay::None;
  return parseRuntimeDisplayWebValue(value, display) &&
         runtimeDisplayKind(display, kind);
}

bool audioKindForValue(const char* value, AudioOutputKind& kind) {
  if (strcmp(value, "pcm5102a") == 0) kind = AudioOutputKind::Pcm5102a;
  else if (strcmp(value, "max98357a") == 0) kind = AudioOutputKind::Max98357;
  else if (strcmp(value, "dspmini") == 0) kind = AudioOutputKind::DspMini;
  else return false;
  return true;
}

const char* audioValue(AudioOutputKind kind) {
  switch (kind) {
    case AudioOutputKind::Pcm5102a: return "pcm5102a";
    case AudioOutputKind::Max98357: return "max98357a";
    case AudioOutputKind::DspMini: return "dspmini";
    case AudioOutputKind::None: return "none";
  }
  return nullptr;
}

const char* audioLabel(AudioOutputKind kind) {
  switch (kind) {
    case AudioOutputKind::Pcm5102a: return "PCM5102A";
    case AudioOutputKind::Max98357: return "MAX98357A";
    case AudioOutputKind::DspMini: return "DSPmini";
    case AudioOutputKind::None: return "Brak";
  }
  return nullptr;
}

bool displayPhysicallyValidated(BoardId board, RuntimeDisplay display) {
  if (display == RuntimeDisplay::None) return true;
  return board == BoardId::BoardM0 &&
         (display == RuntimeDisplay::Ssd1306_128x64 ||
          display == RuntimeDisplay::Ssd1309_128x64);
}

bool inputPhysicallyValidated(BoardId board, RuntimeInput input) {
  if (input == RuntimeInput::None) return true;
  return board == BoardId::BoardM0 && input == RuntimeInput::Ec11;
}

bool audioPhysicallyValidated(BoardId board, AudioOutputKind audio) {
  return board == BoardId::BoardM0 && audio == AudioOutputKind::Pcm5102a;
}

HardwareRoadmapStatus supportedStatus(bool supported, bool physical) {
  return !supported ? HardwareRoadmapStatus::Planned
                    : physical ? HardwareRoadmapStatus::Supported
                               : HardwareRoadmapStatus::Untested;
}

bool moduleSupported(const HardwareCapabilities& capabilities,
                     const char* value) {
  if (strcmp(value, "voxonebt") == 0) return capabilities.supportsVoxOneBt;
  if (strcmp(value, "ds3231") == 0) return capabilities.supportsRtc;
  return false;
}

const char* runtimeDisplayWebLabel(RuntimeDisplay display) {
  switch (display) {
    case RuntimeDisplay::None: return "Brak";
    case RuntimeDisplay::Ssd1306_128x64: return "SSD1306_128X64";
    case RuntimeDisplay::Ssd1309_128x64: return "SSD1309_128X64";
    case RuntimeDisplay::St7789_284x76: return "ST7789_284X76";
    case RuntimeDisplay::St7796s_480x320: return "ST7796S_480X320";
  }
  return nullptr;
}

const char* runtimeInputWebLabel(RuntimeInput input) {
  switch (input) {
    case RuntimeInput::None: return "Brak";
    case RuntimeInput::Ec11: return "EC11";
    case RuntimeInput::Buttons3: return "BUTTONS3";
  }
  return nullptr;
}

bool displaySupported(RuntimeDisplay display,
                      const HardwareCapabilities& capabilities) {
  DisplayKind kind = DisplayKind::None;
  return runtimeDisplayKind(display, kind) &&
         (kind == DisplayKind::None || capabilities.supportsDisplay(kind));
}

bool inputSupported(RuntimeInput input,
                    const HardwareCapabilities& capabilities) {
  LocalInputKind kind = LocalInputKind::None;
  return runtimeInputKind(input, kind) && capabilities.supportsInput(kind);
}

bool audioSupported(RuntimeAudioOutput audio,
                    const HardwareCapabilities& capabilities) {
  AudioOutputKind kind = AudioOutputKind::None;
  return runtimeAudioOutputKind(audio, kind) &&
         capabilities.supportsAudioOutput(kind);
}

class JsonWriter {
 public:
  JsonWriter(char* output, size_t capacity)
      : output_(output), capacity_(capacity), used_(0), valid_(output && capacity) {
    if (valid_) output_[0] = '\0';
  }

  bool append(const char* format, ...) {
    if (!valid_) return false;
    va_list arguments;
    va_start(arguments, format);
    const int length = vsnprintf(output_ + used_, capacity_ - used_, format,
                                 arguments);
    va_end(arguments);
    if (length < 0 || static_cast<size_t>(length) >= capacity_ - used_) {
      valid_ = false;
      return false;
    }
    used_ += static_cast<size_t>(length);
    return true;
  }

  bool valid() const { return valid_; }

 private:
  char* output_;
  size_t capacity_;
  size_t used_;
  bool valid_;
};

bool appendOptions(JsonWriter& writer, const HardwareDescriptor& hardware,
                   bool displays) {
  RuntimeHardwareWebOption options[5];
  const size_t count = displays
      ? supportedRuntimeDisplayOptions(hardware.capabilities, options, 5)
      : supportedRuntimeInputOptions(hardware.capabilities, options, 5);
  for (size_t index = 0; index < count; ++index) {
    if (index && !writer.append(",")) return false;
    HardwareRoadmapStatus status = HardwareRoadmapStatus::Untested;
    if (displays) {
      RuntimeDisplay display = RuntimeDisplay::None;
      if (!parseRuntimeDisplayWebValue(options[index].value, display)) return false;
      status = supportedStatus(
          true, displayPhysicallyValidated(hardware.board.id, display));
    } else {
      RuntimeInput input = RuntimeInput::None;
      if (!parseRuntimeInputWebValue(options[index].value, input)) return false;
      status = supportedStatus(
          true, inputPhysicallyValidated(hardware.board.id, input));
    }
    if (!writer.append(
            "{\"value\":\"%s\",\"label\":\"%s\",\"status\":\"%s\","
            "\"statusLabel\":\"%s\"}",
            options[index].value, options[index].label,
            hardwareRoadmapStatusValue(status),
            hardwareRoadmapStatusLabel(status))) return false;
  }
  return true;
}

bool appendAudioOptions(JsonWriter& writer,
                        const HardwareCapabilities& capabilities) {
  RuntimeHardwareWebOption options[4];
  const size_t count =
      supportedRuntimeAudioOutputOptions(capabilities, options, 4);
  for (size_t index = 0; index < count; ++index) {
    if (index && !writer.append(",")) return false;
    if (!writer.append("{\"value\":\"%s\",\"label\":\"%s\"}",
                       options[index].value, options[index].label))
      return false;
  }
  return true;
}

bool appendRoadmap(JsonWriter& writer, const HardwareRoadmapItem* items,
                   size_t count) {
  for (size_t index = 0; index < count; ++index) {
    if (index && !writer.append(",")) return false;
    const HardwareRoadmapItem& item = items[index];
    if (!writer.append(
            "{\"value\":\"%s\",\"label\":\"%s\",\"note\":\"%s\","
            "\"status\":\"%s\",\"statusLabel\":\"%s\","
            "\"selectable\":%s,\"current\":%s}",
            item.value, item.label, item.note,
            hardwareRoadmapStatusValue(item.status),
            hardwareRoadmapStatusLabel(item.status),
            item.selectable ? "true" : "false",
            item.current ? "true" : "false")) return false;
  }
  return true;
}

}  // namespace

const char* hardwareRoadmapStatusValue(HardwareRoadmapStatus status) {
  switch (status) {
    case HardwareRoadmapStatus::Supported: return "supported";
    case HardwareRoadmapStatus::Untested: return "untested";
    case HardwareRoadmapStatus::Planned: return "planned";
  }
  return "planned";
}

const char* hardwareRoadmapStatusLabel(HardwareRoadmapStatus status) {
  switch (status) {
    case HardwareRoadmapStatus::Supported: return "DOSTĘPNE";
    case HardwareRoadmapStatus::Untested: return "DO TESTU";
    case HardwareRoadmapStatus::Planned: return "PLANOWANE";
  }
  return "PLANOWANE";
}

const char* runtimeDisplayWebValue(RuntimeDisplay display) {
  switch (display) {
    case RuntimeDisplay::None: return "none";
    case RuntimeDisplay::Ssd1306_128x64: return "ssd1306_128x64";
    case RuntimeDisplay::Ssd1309_128x64: return "ssd1309_128x64";
    case RuntimeDisplay::St7789_284x76: return "st7789_284x76";
    case RuntimeDisplay::St7796s_480x320: return "st7796s_480x320";
  }
  return nullptr;
}

const char* runtimeInputWebValue(RuntimeInput input) {
  switch (input) {
    case RuntimeInput::None: return "none";
    case RuntimeInput::Ec11: return "ec11";
    case RuntimeInput::Buttons3: return "buttons3";
  }
  return nullptr;
}

const char* runtimeAudioOutputWebValue(RuntimeAudioOutput audio) {
  switch (audio) {
    case RuntimeAudioOutput::None: return "none";
    case RuntimeAudioOutput::Pcm5102a: return "pcm5102a";
    case RuntimeAudioOutput::DspMini: return "dspmini";
    case RuntimeAudioOutput::Max98357: return "max98357a";
  }
  return nullptr;
}

bool parseRuntimeDisplayWebValue(const char* value, RuntimeDisplay& display) {
  if (!value) return false;
  for (RuntimeDisplay candidate : kDisplays) {
    if (strcmp(value, runtimeDisplayWebValue(candidate)) == 0) {
      display = candidate;
      return true;
    }
  }
  return false;
}

bool parseRuntimeInputWebValue(const char* value, RuntimeInput& input) {
  if (!value) return false;
  for (RuntimeInput candidate : kInputs) {
    if (strcmp(value, runtimeInputWebValue(candidate)) == 0) {
      input = candidate;
      return true;
    }
  }
  return false;
}

bool parseRuntimeAudioOutputWebValue(const char* value,
                                     RuntimeAudioOutput& audio) {
  if (!value) return false;
  for (RuntimeAudioOutput candidate : kAudioOutputs) {
    if (strcmp(value, runtimeAudioOutputWebValue(candidate)) == 0) {
      audio = candidate;
      return true;
    }
  }
  return false;
}

size_t supportedRuntimeDisplayOptions(
    const HardwareCapabilities& capabilities, RuntimeHardwareWebOption* output,
    size_t capacity) {
  size_t count = 0;
  for (RuntimeDisplay display : kDisplays) {
    if (!displaySupported(display, capabilities)) continue;
    if (output && count < capacity)
      output[count] = {runtimeDisplayWebValue(display),
                       runtimeDisplayWebLabel(display)};
    ++count;
  }
  return count;
}

size_t supportedRuntimeInputOptions(
    const HardwareCapabilities& capabilities, RuntimeHardwareWebOption* output,
    size_t capacity) {
  size_t count = 0;
  for (RuntimeInput input : kInputs) {
    if (!inputSupported(input, capabilities)) continue;
    if (output && count < capacity)
      output[count] = {runtimeInputWebValue(input), runtimeInputWebLabel(input)};
    ++count;
  }
  return count;
}

size_t supportedRuntimeAudioOutputOptions(
    const HardwareCapabilities& capabilities, RuntimeHardwareWebOption* output,
    size_t capacity) {
  size_t count = 0;
  for (RuntimeAudioOutput audio : kAudioOutputs) {
    if (!audioSupported(audio, capabilities)) continue;
    AudioOutputKind kind = AudioOutputKind::None;
    if (!runtimeAudioOutputKind(audio, kind)) continue;
    if (output && count < capacity)
      output[count] = {runtimeAudioOutputWebValue(audio), audioLabel(kind)};
    ++count;
  }
  return count;
}

size_t hardwareDisplayRoadmap(const HardwareDescriptor& hardware,
                              const RuntimeHardwareConfig& config,
                              HardwareRoadmapItem* output, size_t capacity) {
  const CatalogItem* items = nullptr;
  size_t itemCount = 0;
  displayCatalog(hardware.board.id, items, itemCount);
  for (size_t index = 0; index < itemCount; ++index) {
    DisplayKind kind = DisplayKind::None;
    const bool known = displayKindForValue(items[index].value, kind);
    const bool supported = known &&
        (kind == DisplayKind::None || hardware.capabilities.supportsDisplay(kind));
    RuntimeDisplay runtime = RuntimeDisplay::None;
    const bool runtimeKnown = parseRuntimeDisplayWebValue(items[index].value,
                                                           runtime);
    const HardwareRoadmapStatus status = supportedStatus(
        supported, runtimeKnown &&
                       displayPhysicallyValidated(hardware.board.id, runtime));
    if (output && index < capacity)
      output[index] = {items[index].value, items[index].label,
                       items[index].note, status, supported,
                       runtimeKnown && config.display == runtime};
  }
  return itemCount;
}

size_t hardwareInputRoadmap(const HardwareDescriptor& hardware,
                            const RuntimeHardwareConfig& config,
                            HardwareRoadmapItem* output, size_t capacity) {
  const size_t count = sizeof(kInputCatalog) / sizeof(kInputCatalog[0]);
  for (size_t index = 0; index < count; ++index) {
    RuntimeInput input = RuntimeInput::None;
    LocalInputKind kind = LocalInputKind::None;
    const bool known = parseRuntimeInputWebValue(kInputCatalog[index].value,
                                                 input) &&
                       runtimeInputKind(input, kind);
    const bool supported = known && hardware.capabilities.supportsInput(kind);
    const HardwareRoadmapStatus status = supportedStatus(
        supported, known && inputPhysicallyValidated(hardware.board.id, input));
    if (output && index < capacity)
      output[index] = {kInputCatalog[index].value, kInputCatalog[index].label,
                       kInputCatalog[index].note, status, supported,
                       known && config.input == input};
  }
  return count;
}

size_t hardwareAudioRoadmap(const HardwareDescriptor& hardware,
                            HardwareRoadmapItem* output, size_t capacity) {
  const CatalogItem* items = nullptr;
  size_t itemCount = 0;
  audioCatalog(hardware.board.id, items, itemCount);
  const char* currentValue = audioValue(hardware.audioOutputKind);
  bool currentInCatalog = false;
  for (size_t index = 0; index < itemCount; ++index)
    if (strcmp(items[index].value, currentValue) == 0) currentInCatalog = true;

  size_t resultIndex = 0;
  if (!currentInCatalog && hardware.audioOutputKind != AudioOutputKind::None) {
    const bool supported = hardware.capabilities.supportsAudioOutput(
        hardware.audioOutputKind);
    if (output && resultIndex < capacity)
      output[resultIndex] = {
          currentValue, audioLabel(hardware.audioOutputKind),
          "Bieżący prototyp", supportedStatus(
              supported, audioPhysicallyValidated(hardware.board.id,
                                                   hardware.audioOutputKind)),
          false, true};
    ++resultIndex;
  }
  for (size_t index = 0; index < itemCount; ++index, ++resultIndex) {
    AudioOutputKind kind = AudioOutputKind::None;
    const bool known = audioKindForValue(items[index].value, kind);
    const bool supported = known && hardware.capabilities.supportsAudioOutput(kind);
    if (output && resultIndex < capacity)
      output[resultIndex] = {
          items[index].value, items[index].label, items[index].note,
          supportedStatus(supported, known &&
              audioPhysicallyValidated(hardware.board.id, kind)),
          false, known && hardware.audioOutputKind == kind};
  }
  return resultIndex;
}

size_t hardwareModuleRoadmap(const HardwareDescriptor& hardware,
                             HardwareRoadmapItem* output, size_t capacity) {
  const CatalogItem* items = nullptr;
  size_t itemCount = 0;
  moduleCatalog(hardware.board.id, items, itemCount);
  for (size_t index = 0; index < itemCount; ++index) {
    const bool supported = moduleSupported(hardware.capabilities,
                                           items[index].value);
    if (output && index < capacity)
      output[index] = {items[index].value, items[index].label,
                       items[index].note,
                       supportedStatus(supported, false), false, false};
  }
  return itemCount;
}

RuntimeHardwareValidation makeRuntimeHardwareWebCandidate(
    const RuntimeHardwareConfig& current,
    const HardwareCapabilities& capabilities, const char* displayValue,
    const char* inputValue, RuntimeHardwareConfig& candidate) {
  candidate = current;
  if (!parseRuntimeDisplayWebValue(displayValue, candidate.display) ||
      !parseRuntimeInputWebValue(inputValue, candidate.input))
    return RuntimeHardwareValidation::Invalid;
  return validateRuntimeHardwareConfig(candidate, capabilities);
}

RuntimeHardwareValidation makeRuntimeHardwareWebCandidate(
    const RuntimeHardwareConfig& current,
    const HardwareCapabilities& capabilities, const char* displayValue,
    const char* inputValue, const char* audioValue,
    RuntimeHardwareConfig& candidate) {
  candidate = current;
  if (!parseRuntimeDisplayWebValue(displayValue, candidate.display) ||
      !parseRuntimeInputWebValue(inputValue, candidate.input) ||
      !parseRuntimeAudioOutputWebValue(audioValue, candidate.audio))
    return RuntimeHardwareValidation::Invalid;
  AudioOutputKind audio = AudioOutputKind::None;
  if (!runtimeAudioOutputKind(candidate.audio, audio))
    return RuntimeHardwareValidation::Invalid;
  if (!capabilities.supportsAudioOutput(audio))
    return RuntimeHardwareValidation::Unsupported;
  return validateRuntimeHardwareConfig(candidate, capabilities);
}

bool formatRuntimeHardwareWebJson(char* output, size_t capacity,
                                  const HardwareDescriptor& hardware,
                                  const RuntimeHardwareConfig& config) {
  const char* display = runtimeDisplayWebValue(config.display);
  const char* input = runtimeInputWebValue(config.input);
  const char* audio = runtimeAudioOutputWebValue(config.audio);
  if (!display || !input || !audio ||
      validateRuntimeHardwareConfig(config, hardware.capabilities) !=
          RuntimeHardwareValidation::Valid)
    return false;
  const char* mcu = hardware.board.mcu == McuFamily::Esp32S3
                        ? "ESP32-S3"
                        : "ESP32";
  JsonWriter writer(output, capacity);
  writer.append("{\"family\":\"%s\",\"mcu\":\"%s\","
                "\"config\":{\"display\":\"%s\",\"input\":\"%s\","
                "\"audio\":\"%s\"},"
                "\"supportedDisplays\":[",
                hardware.board.name, mcu, display, input, audio);
  appendOptions(writer, hardware, true);
  writer.append("],\"supportedInputs\":[");
  appendOptions(writer, hardware, false);
  writer.append("],\"supportedAudioOutputs\":[");
  appendAudioOptions(writer, hardware.capabilities);
  HardwareRoadmapItem roadmap[12];
  size_t count = hardwareDisplayRoadmap(hardware, config, roadmap, 12);
  writer.append("],\"roadmap\":{\"displays\":[");
  appendRoadmap(writer, roadmap, count);
  count = hardwareInputRoadmap(hardware, config, roadmap, 12);
  writer.append("],\"inputs\":[");
  appendRoadmap(writer, roadmap, count);
  count = hardwareAudioRoadmap(hardware, roadmap, 12);
  writer.append("],\"audio\":[");
  appendRoadmap(writer, roadmap, count);
  count = hardwareModuleRoadmap(hardware, roadmap, 12);
  writer.append("],\"modules\":[");
  appendRoadmap(writer, roadmap, count);
  writer.append(" ]},\"restartRequired\":false}");
  return writer.valid();
}

bool formatRuntimeHardwareWebSaveJson(char* output, size_t capacity,
                                      const RuntimeHardwareConfig& config,
                                      bool changed) {
  const char* display = runtimeDisplayWebValue(config.display);
  const char* input = runtimeInputWebValue(config.input);
  const char* audio = runtimeAudioOutputWebValue(config.audio);
  if (!display || !input || !audio || !output || !capacity) return false;
  const int length = snprintf(
      output, capacity,
      "{\"ok\":true,\"changed\":%s,\"restartRequired\":true,"
      "\"config\":{\"display\":\"%s\",\"input\":\"%s\","
      "\"audio\":\"%s\"}}",
      changed ? "true" : "false", display, input, audio);
  return length >= 0 && static_cast<size_t>(length) < capacity;
}

}  // namespace hardware
}  // namespace voxone
