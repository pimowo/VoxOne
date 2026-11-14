#ifndef VOXONE_SOURCE_ICONS_H
#define VOXONE_SOURCE_ICONS_H

#include <stdint.h>

#include "display.h"

// One controller-independent 8x8 icon set.  Renderers may scale the rows for
// TFT panels, while OLED panels consume the same pixels at 1:1 size.
enum class SourceIconKind : uint8_t {
  WebRadio,
  Bluetooth,
  HomeAssistant,
  Dlna,
  Aux,
  Local,
  Spdif,
  Tts,
};

struct SourceIconBitmap {
  uint8_t rows[8];
};

constexpr SourceIconBitmap kSourceIconBitmaps[] = {
    // WEB radio: antenna, mast and receiver.
    {{0x24, 0x42, 0x18, 0x18, 0x7E, 0x42, 0x5A, 0x7E}},
    // Bluetooth rune.
    {{0x10, 0x18, 0x14, 0x52, 0x34, 0x52, 0x14, 0x18}},
    // Home Assistant: house with a connected-node centre.
    {{0x18, 0x3C, 0x7E, 0x42, 0x5A, 0x66, 0x42, 0x7E}},
    // DLNA: media screen and expanding stream marks.
    {{0x70, 0x50, 0x5C, 0x54, 0x56, 0x54, 0x5C, 0x70}},
    // AUX: jack plug and cable.
    {{0x18, 0x18, 0x7E, 0x3C, 0x18, 0x18, 0x08, 0x0E}},
    // Local: SD card outline with contact pads.
    {{0x7C, 0x46, 0x5E, 0x5E, 0x42, 0x5A, 0x42, 0x7E}},
    // SPDIF: optical connector.
    {{0x3C, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x3C}},
    // TTS: speech bubble.
    {{0x7E, 0x42, 0x5A, 0x5A, 0x42, 0x7E, 0x20, 0x10}},
};

constexpr SourceIconKind sourceIconFor(DisplaySourceKind source) {
  return source == DisplaySourceKind::Radio ? SourceIconKind::WebRadio :
         source == DisplaySourceKind::Bluetooth ? SourceIconKind::Bluetooth :
         source == DisplaySourceKind::HomeAssistant
             ? SourceIconKind::HomeAssistant :
         source == DisplaySourceKind::Dlna ? SourceIconKind::Dlna :
         source == DisplaySourceKind::Aux ? SourceIconKind::Aux :
         source == DisplaySourceKind::Local ? SourceIconKind::Local :
         source == DisplaySourceKind::Spdif ? SourceIconKind::Spdif :
         SourceIconKind::Tts;
}

constexpr const SourceIconBitmap& sourceIconBitmap(SourceIconKind icon) {
  return kSourceIconBitmaps[static_cast<uint8_t>(icon)];
}

static_assert(sizeof(kSourceIconBitmaps) / sizeof(kSourceIconBitmaps[0]) == 8,
              "Every source icon identity needs an 8x8 bitmap");
static_assert(sourceIconFor(DisplaySourceKind::Local) == SourceIconKind::Local,
              "Local Files must keep the SD-card icon");

#endif
