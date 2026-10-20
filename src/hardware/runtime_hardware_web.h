#ifndef VOXONE_RUNTIME_HARDWARE_WEB_H
#define VOXONE_RUNTIME_HARDWARE_WEB_H

#include <stddef.h>

#include "runtime_hardware_config.h"

namespace voxone {
namespace hardware {

constexpr size_t kRuntimeHardwareWebJsonCapacity = 4096;

enum class HardwareRoadmapStatus : uint8_t {
  Supported,
  Untested,
  Planned
};

struct RuntimeHardwareWebOption {
  const char* value;
  const char* label;
};

struct HardwareRoadmapItem {
  const char* value;
  const char* label;
  const char* note;
  HardwareRoadmapStatus status;
  bool selectable;
  bool current;
};

const char* hardwareRoadmapStatusValue(HardwareRoadmapStatus status);
const char* hardwareRoadmapStatusLabel(HardwareRoadmapStatus status);

const char* runtimeDisplayWebValue(RuntimeDisplay display);
const char* runtimeInputWebValue(RuntimeInput input);
const char* runtimeAudioOutputWebValue(RuntimeAudioOutput audio);
bool parseRuntimeDisplayWebValue(const char* value, RuntimeDisplay& display);
bool parseRuntimeInputWebValue(const char* value, RuntimeInput& input);
bool parseRuntimeAudioOutputWebValue(const char* value,
                                     RuntimeAudioOutput& audio);

size_t supportedRuntimeDisplayOptions(
    const HardwareCapabilities& capabilities, RuntimeHardwareWebOption* output,
    size_t capacity);
size_t supportedRuntimeInputOptions(
    const HardwareCapabilities& capabilities, RuntimeHardwareWebOption* output,
    size_t capacity);
size_t supportedRuntimeAudioOutputOptions(
    const HardwareCapabilities& capabilities, RuntimeHardwareWebOption* output,
    size_t capacity);

size_t hardwareDisplayRoadmap(const HardwareDescriptor& hardware,
                              const RuntimeHardwareConfig& config,
                              HardwareRoadmapItem* output, size_t capacity);
size_t hardwareInputRoadmap(const HardwareDescriptor& hardware,
                            const RuntimeHardwareConfig& config,
                            HardwareRoadmapItem* output, size_t capacity);
size_t hardwareAudioRoadmap(const HardwareDescriptor& hardware,
                            HardwareRoadmapItem* output, size_t capacity);
size_t hardwareModuleRoadmap(const HardwareDescriptor& hardware,
                             HardwareRoadmapItem* output, size_t capacity);

RuntimeHardwareValidation makeRuntimeHardwareWebCandidate(
    const RuntimeHardwareConfig& current,
    const HardwareCapabilities& capabilities, const char* displayValue,
    const char* inputValue, RuntimeHardwareConfig& candidate);
RuntimeHardwareValidation makeRuntimeHardwareWebCandidate(
    const RuntimeHardwareConfig& current,
    const HardwareCapabilities& capabilities, const char* displayValue,
    const char* inputValue, const char* audioValue,
    RuntimeHardwareConfig& candidate);

bool formatRuntimeHardwareWebJson(char* output, size_t capacity,
                                  const HardwareDescriptor& hardware,
                                  const RuntimeHardwareConfig& config);
bool formatRuntimeHardwareWebSaveJson(char* output, size_t capacity,
                                      const RuntimeHardwareConfig& config,
                                      bool changed);

}  // namespace hardware
}  // namespace voxone

#endif
