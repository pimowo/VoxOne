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
          view.kind == DisplaySourceKind::Bluetooth && view.connected,
          true,
          sources.active() == ActiveSource::Bluetooth
              ? SourceId::Bluetooth : SourceId::Web,
          false};
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

static bool opensSourceCarousel(displayMode_e mode,
                                const SourceManagerState& sources) {
  return resolveUiInput(context(mode, sources), UiInputEvent::Double).action ==
         UiInputAction::OpenSourceSelector;
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

  // RADIO PLAYER: long press opens stations, double-click opens the complete
  // capability-filtered source carousel without changing source immediately.
  assert(longPress(PLAYER, sources) == STATIONS);
  assert(!opensSourceCarousel(STATIONS, sources));
  assert(opensSourceCarousel(PLAYER, sources));
  assert(sources.active() == ActiveSource::Radio);
  sources.select(ActiveSource::Bluetooth, bt);

  // A selected BT source without a phone cannot open transport.
  assert(longPress(PLAYER, sources) == PLAYER);
  assert(opensSourceCarousel(PLAYER, sources));
  assert(sources.active() == ActiveSource::Bluetooth);
  sources.select(ActiveSource::Radio, bt);

  // A phone connection selects BT; transport long press is available.
  bt.connected = true;
  bt.playback = BtPlayback::Playing;
  sources.observe(bt);
  assert(sources.active() == ActiveSource::Bluetooth);
  assert(longPress(PLAYER, sources) == BT_TRANSPORT);
  assert(!opensSourceCarousel(BT_TRANSPORT, sources));
  assert(sources.active() == ActiveSource::Bluetooth);
  assert(longPress(BT_TRANSPORT, sources) == PLAYER);

  // Rotation and click retain the existing transport command mapping.
  DisplaySourceView view{};
  sources.displayView(view);
  assert(view.playback == DisplayPlaybackState::Playing);
  assert(resolveUiInput(context(BT_TRANSPORT, sources), UiInputEvent::Left,
                        UiInputControl::EncoderGesture).action ==
         UiInputAction::BluetoothPrevious);
  assert(resolveUiInput(context(BT_TRANSPORT, sources), UiInputEvent::Right,
                        UiInputControl::EncoderGesture).action ==
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

  // Temporary/TTS audio does not change the selected base source. The local
  // display reports BT as stopped while the temporary decoder owns output,
  // and the AVRCP target remains the connected peer.
  DisplaySourceView temporaryView{};
  sources.displayView(temporaryView, false, false);
  assert(temporaryView.kind == DisplaySourceKind::Bluetooth);
  assert(temporaryView.connected);
  assert(temporaryView.playback == DisplayPlaybackState::Stopped);
  assert(sources.canControlBluetooth());

  // BT PLAYER uses the same carousel gesture; other screens remain unchanged.
  assert(!opensSourceCarousel(VOL, sources));
  assert(sources.active() == ActiveSource::Bluetooth);
  assert(opensSourceCarousel(PLAYER, sources));
  assert(sources.active() == ActiveSource::Bluetooth);
  sources.select(ActiveSource::Radio, bt);

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
  const SourceUpdate disconnected = sources.observe(bt);
  assert(disconnected.reason == SourceChangeReason::BtDisconnect);
  assert(sources.active() == ActiveSource::Radio);
  assert(resolveUiInput(context(BT_TRANSPORT, sources), UiInputEvent::Left,
                        UiInputControl::EncoderGesture).action ==
         UiInputAction::None);
  assert(resolveUiInput(context(BT_TRANSPORT, sources), UiInputEvent::Long).action ==
         UiInputAction::ShowPlayer);
  sources.cycle(bt);  // Manually select BT while the phone is disconnected.
  assert(sources.active() == ActiveSource::Bluetooth);
  click(PLAYER, sources, calls);
  assert(calls.radioToggle == 1 && calls.btPause == 1 && calls.btPlay == 3);

  bt.runtimeAvailable = false;
  const SourceUpdate offline = sources.observe(bt);
  assert(offline.reason == SourceChangeReason::BtOffline);
  assert(sources.active() == ActiveSource::Radio);
}
