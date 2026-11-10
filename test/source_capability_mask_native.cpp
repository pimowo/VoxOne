#include "../src/core/source_catalog.h"

#include <cassert>

int main() {
  const uint8_t w0 = sourceMaskForCapabilities(false, false, false, false);
  assert(w0 == sourceBit(SourceId::Web));
  assert(!sourceIncluded(w0, SourceId::HomeAssistant));
  assert(!sourceIncluded(w0, SourceId::Dlna));

  const uint8_t full = sourceMaskForCapabilities(true, true, true, true);
  assert(sourceIncluded(full, SourceId::Web));
  assert(sourceIncluded(full, SourceId::Bluetooth));
  assert(sourceIncluded(full, SourceId::HomeAssistant));
  assert(sourceIncluded(full, SourceId::Dlna));
  assert(sourceIncluded(full, SourceId::Aux));

  assert(nextSourceInMask(full, SourceId::Web) == SourceId::Bluetooth);
  assert(nextSourceInMask(full, SourceId::Bluetooth) ==
         SourceId::HomeAssistant);
  assert(nextSourceInMask(full, SourceId::HomeAssistant) == SourceId::Dlna);
  assert(nextSourceInMask(full, SourceId::Dlna) == SourceId::Aux);
  assert(nextSourceInMask(full, SourceId::Aux) == SourceId::Web);

  const uint8_t sparse = sourceBit(SourceId::Web) |
                         sourceBit(SourceId::Dlna);
  assert(nextSourceInMask(sparse, SourceId::Web) == SourceId::Dlna);
  assert(nextSourceInMask(sparse, SourceId::Dlna) == SourceId::Web);
  assert(nextSourceInMask(w0, SourceId::Web) == SourceId::Web);
}
