#include "../src/core/bt_audio_route_state.h"
#include "../src/core/bt_transport.h"
#include "../src/core/source_manager_state.h"

#include <cassert>

namespace {
BtAudioRouteTarget route(const SourceManagerState& sources,
                         const BtLinkState& bt) {
  return btAudioRouteTarget(sources.active() == ActiveSource::Bluetooth,
                            bt.playback == BtPlayback::Playing,
                            bt.runtimeAvailable && bt.connected,
                            bt.sampleRate, 44100);
}
}

int main() {
  SourceManagerState sources;
  BtLinkState bt{};
  bt.runtimeAvailable = true;
  bt.connected = true;
  bt.sampleRate = 48000;
  bt.playback = BtPlayback::Playing;
  unsigned transportCommands = 0;

  // A connection edge may select BT, but selection never issues transport.
  assert(sources.observe(bt).reason == SourceChangeReason::BtConnect);
  assert(transportCommands == 0);
  assert(route(sources, bt).btOutput);
  DisplaySourceView view{};
  sources.displayView(view);
  assert(view.playback == DisplayPlaybackState::Playing);

  // E: leaving and re-entering BT changes only the local audio route.
  assert(sources.cycle(bt).reason == SourceChangeReason::Manual);
  assert(sources.bluetoothPhysicallyConnected());
  assert(bt.playback == BtPlayback::Playing);
  assert(!route(sources, bt).btOutput && route(sources, bt).radioOutput);
  for (int i = 0; i < 10; ++i)
    assert(!sources.observe(bt).activeChanged);
  assert(sources.active() == ActiveSource::Radio);
  assert(sources.cycle(bt).reason == SourceChangeReason::Manual);
  assert(route(sources, bt).btOutput && transportCommands == 0);

  // F: a paused remote stays paused across the same source switches.
  bt.playback = BtPlayback::Paused;
  assert(sources.observe(bt).titleChanged);
  assert(!route(sources, bt).btOutput);
  sources.cycle(bt);
  sources.cycle(bt);
  sources.displayView(view);
  assert(view.playback == DisplayPlaybackState::Paused);
  assert(!route(sources, bt).btOutput && transportCommands == 0);

  // A later remote event changes routing without an outgoing PLAY command.
  bt.playback = BtPlayback::Playing;
  assert(sources.observe(bt).titleChanged);
  assert(route(sources, bt).btOutput && transportCommands == 0);
  bt.playback = BtPlayback::Paused;
  assert(sources.observe(bt).titleChanged);
  assert(!route(sources, bt).btOutput && transportCommands == 0);

  // Only explicit transport input produces commands.
  sources.displayView(view);
  assert(btTransportAction(BtTransportInput::Toggle, view) ==
         BtTransportAction::Play);
  ++transportCommands;
  bt.playback = BtPlayback::Playing;
  sources.observe(bt);
  sources.displayView(view);
  assert(btTransportAction(BtTransportInput::Toggle, view) ==
         BtTransportAction::Pause);
  ++transportCommands;
  assert(btTransportAction(BtTransportInput::Next, view) ==
         BtTransportAction::Next);
  ++transportCommands;
  assert(btTransportAction(BtTransportInput::Previous, view) ==
         BtTransportAction::Previous);
  ++transportCommands;
  assert(transportCommands == 4);
}
