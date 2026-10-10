#include "../src/core/source_manager_state.h"
#include "../src/core/bt_transport.h"
#include "../src/core/ui_input.h"

#include <cassert>

static UiInputContext context(displayMode_e mode,
                              const SourceManagerState& sources) {
  DisplaySourceView view{};
  sources.displayView(view);
  return {mode, true, true, true,
          sources.active() == ActiveSource::Bluetooth,
          view.kind == DisplaySourceKind::Bluetooth && view.connected};
}

static displayMode_e longPress(displayMode_e mode,
                               const SourceManagerState& sources) {
  switch (resolveUiInput(context(mode, sources), UiInputEvent::Long).action) {
    case UiInputAction::OpenStations: return STATIONS;
    case UiInputAction::OpenBluetoothTransport: return BT_TRANSPORT;
    case UiInputAction::ShowPlayer: return PLAYER;
    default: return mode;
  }
}

static displayMode_e doubleClick(displayMode_e mode, SourceManagerState& sources,
                                 const BtLinkState& bt, unsigned& cycleCalls) {
  if (resolveUiInput(context(mode, sources), UiInputEvent::Double).action !=
      UiInputAction::CycleSource)
    return mode;
  ++cycleCalls;
  sources.cycle(bt);
  return PLAYER;
}

struct ClickCalls {
  unsigned radioToggle = 0;
  unsigned btPlay = 0;
  unsigned btPause = 0;
};

static void click(displayMode_e mode, const SourceManagerState& sources,
                  ClickCalls& calls) {
  switch (resolveUiInput(context(mode, sources), UiInputEvent::Ok).action) {
    case UiInputAction::TogglePlayback:
      ++calls.radioToggle;
      break;
    case UiInputAction::BluetoothToggle: {
      DisplaySourceView view{};
      sources.displayView(view);
      switch (btTransportAction(BtTransportInput::Toggle, view)) {
        case BtTransportAction::Play: ++calls.btPlay; break;
        case BtTransportAction::Pause: ++calls.btPause; break;
        default: break;
      }
      break;
    }
    default: break;
  }
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
  assert(view.playback == DisplayPlaybackState::Playing);
  assert(resolveUiInput(context(BT_TRANSPORT, sources), UiInputEvent::Left).action ==
         UiInputAction::BluetoothPrevious);
  assert(resolveUiInput(context(BT_TRANSPORT, sources), UiInputEvent::Right).action ==
         UiInputAction::BluetoothNext);
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

  // Main encoder click controls only the active source on PLAYER.
  ClickCalls calls;
  click(PLAYER, sources, calls);
  assert(calls.radioToggle == 1 && calls.btPlay == 0 && calls.btPause == 0);

  sources.cycle(bt);
  assert(sources.active() == ActiveSource::Bluetooth);
  bt.playback = BtPlayback::Playing;
  sources.observe(bt);
  DisplaySourceView resumedView{};
  sources.displayView(resumedView);
  assert(resumedView.playback == DisplayPlaybackState::Playing);
  assert(sources.bluetoothAudioOutputAllowed());
  click(PLAYER, sources, calls);
  assert(calls.radioToggle == 1 && calls.btPause == 1 && calls.btPlay == 0);

  bt.playback = BtPlayback::Paused;
  sources.observe(bt);
  click(PLAYER, sources, calls);
  assert(calls.radioToggle == 1 && calls.btPause == 1 && calls.btPlay == 1);

  bt.playback = BtPlayback::Stopped;
  sources.observe(bt);
  click(PLAYER, sources, calls);
  assert(calls.radioToggle == 1 && calls.btPause == 1 && calls.btPlay == 2);

  click(BT_TRANSPORT, sources, calls);
  assert(calls.radioToggle == 1 && calls.btPause == 1 && calls.btPlay == 3);

  bt.connected = false;
  sources.observe(bt);
  sources.cycle(bt);  // Manually select BT while the phone is disconnected.
  assert(sources.active() == ActiveSource::Bluetooth);
  click(PLAYER, sources, calls);
  assert(calls.radioToggle == 1 && calls.btPause == 1 && calls.btPlay == 3);
}
