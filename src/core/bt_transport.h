#ifndef VOXONE_BT_TRANSPORT_H
#define VOXONE_BT_TRANSPORT_H

#include "display.h"

enum class BtTransportInput : uint8_t { Previous, Next, Toggle };
enum class BtTransportAction : uint8_t { None, Previous, Next, Play, Pause };

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
