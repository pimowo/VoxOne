#include "../src/core/ui_input.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {

UiInputContext radio(displayMode_e mode = PLAYER) {
  return {mode, true, true, true, false, false};
}

UiInputContext bluetooth(displayMode_e mode = PLAYER,
                         bool connected = true) {
  return {mode, true, true, true, true, connected};
}

UiInputAction action(const UiInputContext& context, UiInputEvent event) {
  return resolveUiInput(context, event).action;
}

uint8_t applyVolume(uint8_t value, UiInputAction input) {
  int next = value;
  if (input == UiInputAction::VolumeDown) --next;
  if (input == UiInputAction::VolumeUp) ++next;
  if (next < 0) next = 0;
  if (next > 100) next = 100;
  return static_cast<uint8_t>(next);
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  // EC11 and the future 3xSW adapter produce the same physical vocabulary.
  assert(uiInputEvent(EncoderInput::CounterClockwise) ==
         uiInputEvent(Buttons3Input::Left));
  assert(uiInputEvent(EncoderInput::Clockwise) ==
         uiInputEvent(Buttons3Input::Right));
  assert(uiInputEvent(EncoderInput::Click) ==
         uiInputEvent(Buttons3Input::OkClick));
  assert(uiInputEvent(EncoderInput::DoubleClick) ==
         uiInputEvent(Buttons3Input::OkDoubleClick));
  assert(uiInputEvent(EncoderInput::LongPress) ==
         uiInputEvent(Buttons3Input::OkLongPress));
  assert(uiInputEvent(EncoderInput::VeryLongPress) ==
         uiInputEvent(Buttons3Input::OkVeryLongPress));

  // PLAYER / RADIO.
  assert(action(radio(), UiInputEvent::Right) == UiInputAction::VolumeUp);
  assert(action(radio(), UiInputEvent::Left) == UiInputAction::VolumeDown);
  assert(action(radio(), UiInputEvent::Ok) == UiInputAction::TogglePlayback);
  assert(action(radio(), UiInputEvent::Long) == UiInputAction::OpenStations);

  // VOL and STATIONS.
  assert(action(radio(VOL), UiInputEvent::Ok) == UiInputAction::ToggleMute);
  assert(action(radio(STATIONS), UiInputEvent::Right) ==
         UiInputAction::StationNext);
  assert(action(radio(STATIONS), UiInputEvent::Left) ==
         UiInputAction::StationPrevious);
  assert(action(radio(STATIONS), UiInputEvent::Ok) ==
         UiInputAction::StationSelect);

  // Bluetooth behavior is selected by capabilities and runtime source state.
  assert(action(bluetooth(), UiInputEvent::Ok) ==
         UiInputAction::BluetoothToggle);
  assert(action(bluetooth(), UiInputEvent::Double) ==
         UiInputAction::CycleSource);
  assert(action(bluetooth(), UiInputEvent::Long) ==
         UiInputAction::OpenBluetoothTransport);
  assert(action(bluetooth(BT_TRANSPORT), UiInputEvent::Left) ==
         UiInputAction::BluetoothPrevious);
  assert(action(bluetooth(BT_TRANSPORT), UiInputEvent::Right) ==
         UiInputAction::BluetoothNext);
  assert(action(bluetooth(BT_TRANSPORT), UiInputEvent::Ok) ==
         UiInputAction::BluetoothToggle);
  assert(action(bluetooth(BT_TRANSPORT), UiInputEvent::Long) ==
         UiInputAction::ShowPlayer);
  assert(action(bluetooth(PLAYER, false), UiInputEvent::Long) ==
         UiInputAction::None);

  // Source cycling is unavailable without the board capability.
  UiInputContext noBluetooth = radio();
  noBluetooth.supportsBluetooth = false;
  assert(action(noBluetooth, UiInputEvent::Double) == UiInputAction::None);

  // Existing user-volume limits remain 0..100.
  assert(applyVolume(0, action(radio(), UiInputEvent::Left)) == 0);
  assert(applyVolume(100, action(radio(), UiInputEvent::Right)) == 100);
  assert(applyVolume(0, action(radio(), UiInputEvent::Right)) == 1);
  assert(applyVolume(100, action(radio(), UiInputEvent::Left)) == 99);

  // VeryLong is reserved in the event contract and has no behavior yet.
  assert(action(radio(), UiInputEvent::VeryLong) == UiInputAction::None);

  const std::string implementation = readFile("src/core/ui_input.cpp");
  for (const char* profile : {"PROFILE_A0", "PROFILE_B0", "PROFILE_C0",
                              "PROFILE_X0", "VOXONE_PROFILE_"})
    assert(implementation.find(profile) == std::string::npos);
  assert(implementation.find("hardwareCapabilities()") != std::string::npos);
}
