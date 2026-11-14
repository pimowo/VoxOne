#ifndef VOXONE_SOURCE_CATALOG_H
#define VOXONE_SOURCE_CATALOG_H

#include <stdint.h>
#include <string.h>

// Append new identities: SourceId values are exchanged by the runtime UI and
// must remain stable across firmware updates.
enum class SourceId : uint8_t {
  Web,
  Bluetooth,
  HomeAssistant,
  Dlna,
  Aux,
  Local,
};

constexpr uint8_t sourceBit(SourceId source) {
  return static_cast<uint8_t>(1U << static_cast<uint8_t>(source));
}

constexpr uint8_t kCoreSourceMask =
    sourceBit(SourceId::Web);

constexpr uint8_t sourceMaskForCapabilities(bool supportsBluetooth,
                                            bool supportsHa,
                                            bool supportsDlna,
                                            bool supportsAux,
                                            bool supportsLocal) {
  return kCoreSourceMask |
      (supportsBluetooth ? sourceBit(SourceId::Bluetooth) : 0U) |
      (supportsHa ? sourceBit(SourceId::HomeAssistant) : 0U) |
      (supportsDlna ? sourceBit(SourceId::Dlna) : 0U) |
      (supportsAux ? sourceBit(SourceId::Aux) : 0U) |
      (supportsLocal ? sourceBit(SourceId::Local) : 0U);
}

constexpr bool sourceIncluded(uint8_t mask, SourceId source) {
  return (mask & sourceBit(source)) != 0;
}

constexpr const char* sourceApiName(SourceId source) {
  return source == SourceId::Web ? "radio" :
         source == SourceId::Bluetooth ? "bt" :
         source == SourceId::HomeAssistant ? "ha" :
         source == SourceId::Dlna ? "dlna" :
         source == SourceId::Aux ? "aux" : "local";
}

constexpr const char* sourceMenuLabel(SourceId source) {
  return source == SourceId::Web ? "WEB Radio" :
         source == SourceId::Bluetooth ? "Bluetooth" :
         source == SourceId::HomeAssistant ? "Home Assistant" :
         source == SourceId::Dlna ? "DLNA" :
         source == SourceId::Aux ? "AUX" : "SD / Local";
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
  else if (strcmp(value, "local") == 0 || strcmp(value, "sd") == 0)
    source = SourceId::Local;
  else
    return false;
  return true;
}

#endif
