#include <cassert>
#include <cstring>
#include "../src/core/eq_preset_label.h"

int main() {
  assert(std::strcmp(eqPresetLabel(0, 0, 0), "FLAT") == 0);
  assert(std::strcmp(eqPresetLabel(6, -2, 0), "BASS") == 0);
  assert(std::strcmp(eqPresetLabel(5, -2, 4), "ROCK") == 0);
  assert(std::strcmp(eqPresetLabel(3, 2, 3), "POP") == 0);
  assert(std::strcmp(eqPresetLabel(-4, 4, 2), "MOWA") == 0);
  assert(std::strcmp(eqPresetLabel(1, 0, 0), "USER") == 0);

  // The label depends only on the shared tone settings, not the active source.
  const char* radioLabel = eqPresetLabel(5, -2, 4);
  const char* bluetoothLabel = eqPresetLabel(5, -2, 4);
  assert(std::strcmp(radioLabel, bluetoothLabel) == 0);
}
