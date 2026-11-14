#include "../src/core/source_icons.h"

#include <cassert>

int main() {
  const DisplaySourceKind sources[] = {
      DisplaySourceKind::Radio, DisplaySourceKind::Bluetooth,
      DisplaySourceKind::HomeAssistant, DisplaySourceKind::Dlna,
      DisplaySourceKind::Aux, DisplaySourceKind::Local};
  for (const auto source : sources) {
    const auto icon = sourceIconFor(source);
    const auto& bitmap = sourceIconBitmap(icon);
    uint8_t pixels = 0;
    for (uint8_t row : bitmap.rows) pixels |= row;
    assert(pixels != 0);
  }
  assert(sourceIconFor(DisplaySourceKind::Radio) !=
         sourceIconFor(DisplaySourceKind::Bluetooth));
  assert(sourceIconFor(DisplaySourceKind::HomeAssistant) !=
         sourceIconFor(DisplaySourceKind::Dlna));
  assert(sourceIconFor(DisplaySourceKind::Aux) !=
         sourceIconFor(DisplaySourceKind::Local));
}
