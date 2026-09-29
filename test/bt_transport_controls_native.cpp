#include "../src/core/source_manager_state.h"
#include "../src/core/bt_transport.h"

#include <cassert>

static displayMode_e longPress(displayMode_e mode, const SourceManagerState& sources) {
  DisplaySourceView view{};
  sources.displayView(view);
  switch (btEncoderLongPressAction(mode, view)) {
    case BtEncoderLongPressAction::Stations: return STATIONS;
    case BtEncoderLongPressAction::Transport: return BT_TRANSPORT;
    case BtEncoderLongPressAction::Player: return PLAYER;
    case BtEncoderLongPressAction::None: return mode;
  }
  return mode;
}

static displayMode_e doubleClick(displayMode_e mode, SourceManagerState& sources,
                                 const BtLinkState& bt, unsigned& cycleCalls) {
  if (!btTransportDoubleClickCyclesSource(mode)) return mode;
  ++cycleCalls;
  sources.cycle(bt);
  return PLAYER;
}

int main() {
  SourceManagerState sources;
  BtLinkState bt{};
  bt.runtimeAvailable = true;
  sources.observe(bt);
  unsigned cycleCalls = 0;

  // RADIO PLAYER: long press opens stations, double-click selects BT.
  assert(longPress(PLAYER, sources) == STATIONS);
  assert(doubleClick(STATIONS, sources, bt, cycleCalls) == STATIONS);
  assert(cycleCalls == 0 && sources.active() == ActiveSource::Radio);
  assert(doubleClick(PLAYER, sources, bt, cycleCalls) == PLAYER);
  assert(cycleCalls == 1 && sources.active() == ActiveSource::Bluetooth);

  // A selected BT source without a phone cannot open transport.
  assert(longPress(PLAYER, sources) == PLAYER);
  assert(doubleClick(PLAYER, sources, bt, cycleCalls) == PLAYER);
  assert(cycleCalls == 2 && sources.active() == ActiveSource::Radio);

  // A phone connection selects BT; transport long press is available.
  bt.connected = true;
  bt.playback = BtPlayback::Playing;
  sources.observe(bt);
  assert(sources.active() == ActiveSource::Bluetooth);
  assert(longPress(PLAYER, sources) == BT_TRANSPORT);
  assert(doubleClick(BT_TRANSPORT, sources, bt, cycleCalls) == BT_TRANSPORT);
  assert(cycleCalls == 2 && sources.active() == ActiveSource::Bluetooth);
  assert(longPress(BT_TRANSPORT, sources) == PLAYER);

  // Rotation and click retain the existing transport command mapping.
  DisplaySourceView view{};
  sources.displayView(view);
  assert(btTransportInputForRotation(-1) == BtTransportInput::Previous);
  assert(btTransportInputForRotation(1) == BtTransportInput::Next);
  assert(btTransportAction(BtTransportInput::Previous, view) == BtTransportAction::Previous);
  assert(btTransportAction(BtTransportInput::Next, view) == BtTransportAction::Next);
  assert(btTransportAction(BtTransportInput::Toggle, view) == BtTransportAction::Pause);
  bt.playback = BtPlayback::Paused;
  sources.observe(bt);
  sources.displayView(view);
  assert(btTransportAction(BtTransportInput::Toggle, view) == BtTransportAction::Play);
  bt.playback = BtPlayback::Stopped;
  sources.observe(bt);
  sources.displayView(view);
  assert(btTransportAction(BtTransportInput::Toggle, view) == BtTransportAction::Play);

  // BT PLAYER: double-click selects RADIO; other screens remain unchanged.
  assert(doubleClick(VOL, sources, bt, cycleCalls) == VOL);
  assert(cycleCalls == 2 && sources.active() == ActiveSource::Bluetooth);
  assert(doubleClick(PLAYER, sources, bt, cycleCalls) == PLAYER);
  assert(cycleCalls == 3 && sources.active() == ActiveSource::Radio);
}
