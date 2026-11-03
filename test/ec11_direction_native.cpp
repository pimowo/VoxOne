#include "../profiles/profile.h"
#include "../src/core/local_input_backend.h"
#include "../src/core/ui_input.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
UiInputAction action(displayMode_e mode, UiInputEvent event) {
  const UiInputContext context{mode, true, true, false, false, false, false};
  return resolveUiInput(context, event).action;
}

UiInputEvent eventForDelta(int8_t delta) {
  return uiInputEvent(delta > 0 ? EncoderInput::Clockwise
                                : EncoderInput::CounterClockwise);
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}
}  // namespace

int main() {
  constexpr bool reversed = VOXONE_EC11_REVERSE_DIRECTION != 0;
#if defined(VOXONE_PROFILE_S0)
  static_assert(reversed, "S0 must normalize its observed reversed EC11 phase");
  static_assert(ENC_BTNL == 9 && ENC_BTNR == 8 && ENC_BTNB == 7,
                "S0 EC11 GPIO must remain A=9, B=8, KEY=7");
#else
  static_assert(!reversed, "Other profiles must retain their EC11 direction");
#endif

  // The physical observation is negative raw delta for S0 CW; established
  // profiles already report positive raw delta for CW.
  const int8_t physicalCwRaw = reversed ? -1 : 1;
  const int8_t physicalCcwRaw = reversed ? 1 : -1;
  const int8_t clockwise = voxone::input::normalizeEc11Delta(
      physicalCwRaw, reversed);
  const int8_t counterClockwise = voxone::input::normalizeEc11Delta(
      physicalCcwRaw, reversed);
  assert(clockwise == 1);
  assert(counterClockwise == -1);

  // Direction is normalized before shared UI dispatch, so volume and station
  // navigation agree without profile-specific behavior in UI core.
  assert(action(PLAYER, eventForDelta(clockwise)) == UiInputAction::VolumeUp);
  assert(action(PLAYER, eventForDelta(counterClockwise)) ==
         UiInputAction::VolumeDown);
  assert(action(STATIONS, eventForDelta(clockwise)) ==
         UiInputAction::StationNext);
  assert(action(STATIONS, eventForDelta(counterClockwise)) ==
         UiInputAction::StationPrevious);

  const std::string controls = readFile("src/core/controls.cpp");
  assert(controls.find("normalizeEc11Delta(") != std::string::npos);
  assert(controls.find("VOXONE_EC11_REVERSE_DIRECTION != 0") !=
         std::string::npos);
}
