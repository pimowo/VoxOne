#include "../src/core/ui_input.h"
#include "../src/core/ui_state.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
UiModeContext ready(bool bluetooth = false) {
  return {true, false, bluetooth};
}

UiInputContext radio(const UiState& state) {
  return {state.mode(), true, true, true, false, false};
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void assertUiStateCriticalSectionsAreRamOnly(const std::string& source) {
  constexpr const char* enter = "portENTER_CRITICAL(&uiStateMux)";
  constexpr const char* leave = "portEXIT_CRITICAL(&uiStateMux)";
  size_t cursor = 0;
  unsigned blocks = 0;
  while ((cursor = source.find(enter, cursor)) != std::string::npos) {
    const size_t end = source.find(leave, cursor);
    assert(end != std::string::npos);
    const std::string block = source.substr(cursor, end - cursor);
    for (const char* forbidden : {"config.", "uiTimeoutConfig(", "millis(",
                                  "updateProgress(", "bluetoothTransport",
                                  "display.", "network.", "player.",
                                  "sourceManager", "Serial.", "delay(",
                                  "yield(", "String", "SPIFFS", "xSemaphore"})
      assert(block.find(forbidden) == std::string::npos);
    cursor = end + std::char_traits<char>::length(leave);
    ++blocks;
  }
  assert(blocks == 5);
}
}  // namespace

int main() {
  UiState state;
  assert(state.mode() == PLAYER);

  // PLAYER input remains an action; the controller owns the resulting mode.
  assert(resolveUiInput(radio(state), UiInputEvent::Right).action ==
         UiInputAction::VolumeUp);
  assert(state.requestMode(VOL, ready()));
  assert(state.mode() == VOL);
  assert(resolveUiInput(radio(state), UiInputEvent::Ok).action ==
         UiInputAction::ToggleMute);

  // Logical return timeouts are owned and evaluated by core state.
  state.armReturnToPlayer(1000, 3, VOL);
  assert(!state.returnToPlayerDue(3999));
  assert(state.returnToPlayerDue(4000));
  assert(state.requestMode(PLAYER, ready()));

  // Station selection keeps the existing 1..N numbering and wrapping.
  assert(resolveUiInput(radio(state), UiInputEvent::Long).action ==
         UiInputAction::OpenStations);
  assert(state.requestMode(STATIONS, ready()));
  state.beginStationSelection(3, 3);
  assert(state.moveStationSelection(1, 3) == 1);
  assert(state.moveStationSelection(-1, 3) == 3);
  state.beginStationSelection(99, 3);
  assert(state.selectedStation() == 1);
  state.beginStationSelection(2, 3);
  assert(state.selectedStation() == 2);
  // A later playlist-length snapshot safely re-normalizes a stale selection.
  state.beginStationSelection(state.selectedStation(), 1);
  assert(state.selectedStation() == 1);
  state.beginStationSelection(1, 0);
  assert(state.selectedStation() == 0);
  assert(state.moveStationSelection(1, 0) == 0);
  assert(resolveUiInput(radio(state), UiInputEvent::Ok).action ==
         UiInputAction::StationSelect);
  assert(state.requestMode(PLAYER, ready()));

  assert(state.requestMode(SCREENSAVER, ready()));
  assert(resolveUiInput(radio(state), UiInputEvent::Ok).action ==
         UiInputAction::WakePlayer);
  assert(state.requestMode(PLAYER, ready()));
  assert(state.requestMode(SCREENBLANK, ready()));
  assert(resolveUiInput(radio(state), UiInputEvent::Double).action ==
         UiInputAction::WakePlayer);
  assert(state.requestMode(PLAYER, ready()));

  // System/update policy blocks navigation takeover while locked.
  UiModeContext locked{true, true, false};
  assert(!state.requestMode(PLAYER, locked));
  assert(state.requestMode(UPDATING, locked));
  assert(!state.requestMode(STATIONS, locked));

  // BT transport is selected by runtime capability/state, never PCB name.
  UiState bt;
  assert(!bt.requestMode(BT_TRANSPORT, ready(false)));
  assert(bt.requestMode(BT_TRANSPORT, ready(true)));
  assert(resolveUiInput({bt.mode(), true, true, true, true, true},
                        UiInputEvent::Long).action ==
         UiInputAction::ShowPlayer);
  assert(bt.requestMode(PLAYER, ready(true)));

  assert(resolveUiInput(radio(state), UiInputEvent::VeryLong).action ==
         UiInputAction::None);

  const std::string input = readFile("src/core/ui_input.cpp");
  assert(input.find("display.mode()") == std::string::npos);
  assert(input.find("uiState.mode()") != std::string::npos);
  assert(input.find("transitionUiMode(") != std::string::npos);
  assert(input.find("normalizeUiStationSelection();") != std::string::npos);
  assert(input.find("if (selected != 0) display.putRequest(CLOSEPLAYLIST, selected);") !=
         std::string::npos);
  const std::string display = readFile("src/core/display.h");
  assert(display.find("Display::_mode") == std::string::npos);
  assert(display.find("currentPlItem") == std::string::npos);
  const std::string uiStateRuntime = readFile("src/core/ui_state.cpp");
  assertUiStateCriticalSectionsAreRamOnly(uiStateRuntime);
}
