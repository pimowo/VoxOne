#ifndef VOXONE_SOURCE_CATALOG_H
#define VOXONE_SOURCE_CATALOG_H

#include <stdint.h>
#include <string.h>

enum class SourceId : uint8_t { Web, Bluetooth, HomeAssistant, Dlna, Aux };

constexpr uint8_t sourceBit(SourceId source) {
  return static_cast<uint8_t>(1U << static_cast<uint8_t>(source));
}

constexpr uint8_t kCoreSourceMask =
    sourceBit(SourceId::Web);

constexpr uint8_t sourceMaskForCapabilities(bool supportsBluetooth,
                                            bool supportsHa,
                                            bool supportsDlna,
                                            bool supportsAux) {
  return kCoreSourceMask |
      (supportsBluetooth ? sourceBit(SourceId::Bluetooth) : 0U) |
      (supportsHa ? sourceBit(SourceId::HomeAssistant) : 0U) |
      (supportsDlna ? sourceBit(SourceId::Dlna) : 0U) |
      (supportsAux ? sourceBit(SourceId::Aux) : 0U);
}

constexpr bool sourceIncluded(uint8_t mask, SourceId source) {
  return (mask & sourceBit(source)) != 0;
}

inline SourceId nextSourceInMask(uint8_t mask, SourceId current) {
  constexpr uint8_t kSourceCount =
      static_cast<uint8_t>(SourceId::Aux) + 1U;
  const uint8_t start = static_cast<uint8_t>(current);
  for (uint8_t offset = 1; offset <= kSourceCount; ++offset) {
    const SourceId candidate = static_cast<SourceId>(
        static_cast<uint8_t>((start + offset) % kSourceCount));
    if (sourceIncluded(mask, candidate)) return candidate;
  }
  return SourceId::Web;
}

constexpr const char* sourceApiName(SourceId source) {
  return source == SourceId::Web ? "radio" :
         source == SourceId::Bluetooth ? "bt" :
         source == SourceId::HomeAssistant ? "ha" :
         source == SourceId::Dlna ? "dlna" : "aux";
}

constexpr const char* sourceMenuLabel(SourceId source) {
  return source == SourceId::Web ? "WEB" :
         source == SourceId::Bluetooth ? "BT" :
         source == SourceId::HomeAssistant ? "HA" :
         source == SourceId::Dlna ? "DLNA" : "AUX";
}

inline bool parseSourceId(const char* value, SourceId& source) {
  if (!value) return false;
  if (strcmp(value, "radio") == 0 || strcmp(value, "web") == 0)
    source = SourceId::Web;
  else if (strcmp(value, "bt") == 0)
    source = SourceId::Bluetooth;
  else if (strcmp(value, "ha") == 0)
    source = SourceId::HomeAssistant;
  else if (strcmp(value, "dlna") == 0)
    source = SourceId::Dlna;
  else if (strcmp(value, "aux") == 0)
    source = SourceId::Aux;
  else
    return false;
  return true;
}

#endif
