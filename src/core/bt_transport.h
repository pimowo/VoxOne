#ifndef VOXONE_BT_TRANSPORT_H
#define VOXONE_BT_TRANSPORT_H

#include "display.h"

enum class BtTransportInput : uint8_t { Previous, Next, Toggle };
enum class BtTransportAction : uint8_t { None, Previous, Next, Play, Pause };
enum class BtEncoderLongPressAction : uint8_t { None, Stations, Transport, Player };

inline bool btTransportDoubleClickCyclesSource(displayMode_e mode) {
  return mode == PLAYER;
}

inline BtEncoderLongPressAction btEncoderLongPressAction(
    displayMode_e mode, const DisplaySourceView& source) {
  if (mode == BT_TRANSPORT) return BtEncoderLongPressAction::Player;
  if (mode != PLAYER) return BtEncoderLongPressAction::None;
  if (source.kind == DisplaySourceKind::Radio)
    return BtEncoderLongPressAction::Stations;
  return source.connected ? BtEncoderLongPressAction::Transport
                          : BtEncoderLongPressAction::None;
}

inline BtTransportInput btTransportInputForRotation(int8_t delta) {
  return delta < 0 ? BtTransportInput::Previous : BtTransportInput::Next;
}

inline BtTransportAction btTransportAction(BtTransportInput input,
                                           const DisplaySourceView& source) {
  if (source.kind != DisplaySourceKind::Bluetooth || !source.connected)
    return BtTransportAction::None;
  switch (input) {
    case BtTransportInput::Previous: return BtTransportAction::Previous;
    case BtTransportInput::Next: return BtTransportAction::Next;
    case BtTransportInput::Toggle:
      return source.playback == DisplayPlaybackState::Playing
                 ? BtTransportAction::Pause : BtTransportAction::Play;
  }
  return BtTransportAction::None;
}

#endif
