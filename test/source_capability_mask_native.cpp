#include "../src/core/source_catalog.h"

#include <cassert>

int main() {
  const uint8_t minimal = sourceMaskForCapabilities(false, false, false, false);
  assert(minimal == sourceBit(SourceId::Web));
  assert(!sourceIncluded(minimal, SourceId::HomeAssistant));
  assert(!sourceIncluded(minimal, SourceId::Dlna));

  const uint8_t full = sourceMaskForCapabilities(true, true, true, true);
  assert(sourceIncluded(full, SourceId::Web));
  assert(sourceIncluded(full, SourceId::Bluetooth));
  assert(sourceIncluded(full, SourceId::HomeAssistant));
  assert(sourceIncluded(full, SourceId::Dlna));
  assert(sourceIncluded(full, SourceId::Aux));
  assert(full == (sourceBit(SourceId::Web) |
                  sourceBit(SourceId::Bluetooth) |
                  sourceBit(SourceId::HomeAssistant) |
                  sourceBit(SourceId::Dlna) |
                  sourceBit(SourceId::Aux)));
  assert(sourceMenuLabel(SourceId::Web)[0] == 'W');
  assert(sourceMenuLabel(SourceId::Bluetooth)[0] == 'B');
  assert(sourceMenuLabel(SourceId::HomeAssistant)[0] == 'H');
  assert(sourceMenuLabel(SourceId::Dlna)[0] == 'D');
  assert(sourceMenuLabel(SourceId::Aux)[0] == 'A');
}
