#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../src/core/line_out_config.h"
#include "../src/core/line_out_volume.h"
#include "../src/core/ui_input.h"

int main() {
  uint8_t storage[79]{};
  bool enabled = true;
  uint8_t volume = 0;

  assert(!voxone::line_out::decode(storage, sizeof(storage), enabled, volume));
  assert(!enabled && volume == 100);
  assert(voxone::line_out::encode(true, 80, storage, sizeof(storage)));
  assert(voxone::line_out::decode(storage, sizeof(storage), enabled, volume));
  assert(enabled && volume == 80);
  assert(!voxone::line_out::encode(true, 101, storage, sizeof(storage)));
  assert(voxone::line_out::maximumOrDefault(80) == 80);
  assert(voxone::line_out::maximumOrDefault(0) == 100);
  assert(voxone::line_out::clampToMaximum(100, 80) == 80);
  assert(voxone::line_out::clampToMaximum(75, 80) == 75);

  voxone::config_runtime::Snapshot runtime{};
  runtime.volume = 25;
  runtime.userVolume = 10;
  runtime.maximumVolume = 80;
  assert(voxone::line_out::effectiveRawVolume(runtime) == 25);
  assert(voxone::line_out::effectiveUserVolume(runtime) == 10);
  runtime.lineOutEnabled = true;
  runtime.lineOutVolume = 100;
  assert(voxone::line_out::effectiveRawVolume(runtime) ==
         volumeUserToRaw(80, 80));
  assert(voxone::line_out::effectiveUserVolume(runtime) == 80);
  runtime.maximumVolume = 1;
  runtime.lineOutVolume = 75;
  assert(voxone::line_out::effectiveRawVolume(runtime) ==
         volumeUserToRaw(1, 1));
  assert(voxone::line_out::effectiveUserVolume(runtime) == 1);

  const UiInputContext normal{PLAYER, true, true, false, false, false,
                              false, SourceId::Web, false, false};
  assert(resolveUiInput(normal, UiInputEvent::Right,
                        UiInputControl::EncoderRotation).action ==
         UiInputAction::VolumeUp);
  const UiInputContext lineOut{PLAYER, true, true, false, false, false,
                               false, SourceId::Web, false, true};
  assert(resolveUiInput(lineOut, UiInputEvent::Right,
                        UiInputControl::EncoderRotation).action ==
         UiInputAction::SourceNext);
  assert(resolveUiInput(lineOut, UiInputEvent::Left,
                        UiInputControl::EncoderRotation).action ==
         UiInputAction::SourcePrevious);

  return 0;
}
