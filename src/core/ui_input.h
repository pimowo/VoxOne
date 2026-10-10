#ifndef VOXONE_UI_INPUT_H
#define VOXONE_UI_INPUT_H

#include <stdint.h>

#include "common.h"

// Physical input vocabulary. These events deliberately describe direction
// and gestures, not Player, SourceManager or display actions.
enum class UiInputEvent : uint8_t {
  Left,
  Right,
  Ok,
  Double,
  Long,
  VeryLong
};

enum class EncoderInput : uint8_t {
  CounterClockwise,
  Clockwise,
  Click,
  DoubleClick,
  LongPress,
  VeryLongPress
};

enum class Buttons3Input : uint8_t {
  Left,
  Right,
  OkClick,
  OkDoubleClick,
  OkLongPress,
  OkVeryLongPress
};

inline UiInputEvent uiInputEvent(EncoderInput input) {
  switch (input) {
    case EncoderInput::CounterClockwise: return UiInputEvent::Left;
    case EncoderInput::Clockwise: return UiInputEvent::Right;
    case EncoderInput::Click: return UiInputEvent::Ok;
    case EncoderInput::DoubleClick: return UiInputEvent::Double;
    case EncoderInput::LongPress: return UiInputEvent::Long;
    case EncoderInput::VeryLongPress: return UiInputEvent::VeryLong;
  }
  return UiInputEvent::Ok;
}

inline UiInputEvent uiInputEvent(Buttons3Input input) {
  switch (input) {
    case Buttons3Input::Left: return UiInputEvent::Left;
    case Buttons3Input::Right: return UiInputEvent::Right;
    case Buttons3Input::OkClick: return UiInputEvent::Ok;
    case Buttons3Input::OkDoubleClick: return UiInputEvent::Double;
    case Buttons3Input::OkLongPress: return UiInputEvent::Long;
    case Buttons3Input::OkVeryLongPress: return UiInputEvent::VeryLong;
  }
  return UiInputEvent::Ok;
}

enum class UiInputAction : uint8_t {
  None,
  VolumeDown,
  VolumeUp,
  StationPrevious,
  StationNext,
  StationSelect,
  TogglePlayback,
  ToggleMute,
  BluetoothPrevious,
  BluetoothNext,
  BluetoothToggle,
  CycleSource,
  OpenStations,
  OpenBluetoothTransport,
  ShowPlayer,
  WakePlayer
};

struct UiInputContext {
  displayMode_e mode;
  bool networkConnected;
  bool hasLocalDisplay;
  bool supportsBluetooth;
  bool bluetoothSelected;
  bool bluetoothConnected;
};

struct UiInputDecision {
  UiInputAction action;
  bool cancelNumberEntry;
};

constexpr UiInputDecision uiInputDecision(UiInputAction action,
                                          bool cancelNumberEntry = false) {
  return {action, cancelNumberEntry};
}

inline UiInputDecision resolveUiInput(const UiInputContext& context,
                                      UiInputEvent event) {
  if (context.mode == LOST || context.mode == UPDATING)
    return uiInputDecision(UiInputAction::None);

  if (event == UiInputEvent::Left || event == UiInputEvent::Right) {
    if (!context.networkConnected)
      return uiInputDecision(UiInputAction::None);
    if (context.mode == BT_TRANSPORT) {
      if (!context.supportsBluetooth || !context.bluetoothSelected ||
          !context.bluetoothConnected)
        return uiInputDecision(UiInputAction::None);
      return uiInputDecision(event == UiInputEvent::Left
                                 ? UiInputAction::BluetoothPrevious
                                 : UiInputAction::BluetoothNext);
    }
    if (context.mode == STATIONS)
      return uiInputDecision(event == UiInputEvent::Left
                                 ? UiInputAction::StationPrevious
                                 : UiInputAction::StationNext);
    return uiInputDecision(event == UiInputEvent::Left
                               ? UiInputAction::VolumeDown
                               : UiInputAction::VolumeUp,
                           context.mode == NUMBERS);
  }

  if (event == UiInputEvent::Ok) {
    if (context.mode == BT_TRANSPORT)
      return uiInputDecision(context.supportsBluetooth &&
                                     context.bluetoothSelected
                                 ? UiInputAction::BluetoothToggle
                                 : UiInputAction::None);
    if (context.mode == VOL)
      return uiInputDecision(UiInputAction::ToggleMute);
    if (context.mode == NUMBERS)
      return uiInputDecision(UiInputAction::ShowPlayer, true);
    if (context.mode == PLAYER)
      return uiInputDecision(context.supportsBluetooth &&
                                     context.bluetoothSelected
                                 ? UiInputAction::BluetoothToggle
                                 : UiInputAction::TogglePlayback);
    if (context.mode == SCREENSAVER || context.mode == SCREENBLANK)
      return uiInputDecision(UiInputAction::WakePlayer);
    if (context.mode == STATIONS)
      return uiInputDecision(UiInputAction::StationSelect);
    return uiInputDecision(UiInputAction::None);
  }

  if (event == UiInputEvent::Double) {
    if (context.mode == SCREENSAVER || context.mode == SCREENBLANK)
      return uiInputDecision(UiInputAction::WakePlayer);
    if (context.mode == PLAYER && context.supportsBluetooth)
      return uiInputDecision(UiInputAction::CycleSource);
    return uiInputDecision(UiInputAction::None);
  }

  if (event == UiInputEvent::Long) {
    if (!context.hasLocalDisplay)
      return uiInputDecision(UiInputAction::None);
    if (context.mode == BT_TRANSPORT)
      return uiInputDecision(UiInputAction::ShowPlayer);
    if (context.mode == PLAYER) {
      if (context.supportsBluetooth && context.bluetoothSelected)
        return uiInputDecision(context.bluetoothConnected
                                   ? UiInputAction::OpenBluetoothTransport
                                   : UiInputAction::None);
      return uiInputDecision(UiInputAction::OpenStations);
    }
    return uiInputDecision(UiInputAction::ShowPlayer);
  }

  // VeryLong is part of the contract for both physical adapters, but it has
  // intentionally no behavior until a later UI policy stage defines one.
  return uiInputDecision(UiInputAction::None);
}

void dispatchUiInput(UiInputEvent event, int8_t steps = 1);

#endif
