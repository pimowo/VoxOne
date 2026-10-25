#include "../src/core/bt_audio_route_state.h"
#include "../src/core/bt_link_protocol.h"
#include "../src/core/bt_transport.h"
#include "../src/core/source_manager_state.h"

#include <cassert>
#include <string>
#include <vector>

namespace {

struct LinkHarness {
  std::vector<std::string> sent;
  BtLinkProtocol link;

  LinkHarness() : link(&LinkHarness::send, nullptr, this) {}

  static void send(void* context, const char* command) {
    static_cast<LinkHarness*>(context)->sent.emplace_back(command);
  }

  void line(const char* text, uint32_t nowMs) {
    for (const char* byte = text; *byte != '\0'; ++byte)
      link.feed(*byte, nowMs);
    link.feed('\r', nowMs);
    link.feed('\n', nowMs);
  }
};

BtAudioRouteTarget route(const SourceManagerState& source,
                         const BtLinkState& link) {
  return btAudioRouteTarget(
      source.active() == ActiveSource::Bluetooth,
      link.playback == BtPlayback::Playing,
      link.runtimeAvailable && link.connected, link.sampleRate, 44100);
}

void provideConnectedSnapshot(LinkHarness& harness, BtPlayback playback,
                              uint32_t nowMs) {
  harness.line("STATUS_BEGIN", nowMs++);
  harness.line("PROTO 2", nowMs++);
  harness.line("FW_VERSION 0.6.7-dev", nowMs++);
  harness.line("CAPS A2DP AVRCP", nowMs++);
  harness.line("OTA_STATE NOT_PENDING", nowMs++);
  harness.line("CONNECTED", nowMs++);
  harness.line("DEVICE Contract phone", nowMs++);
  harness.line("SAMPLE_RATE 48000", nowMs++);
  switch (playback) {
    case BtPlayback::Playing: harness.line("PLAYING", nowMs++); break;
    case BtPlayback::Paused: harness.line("PAUSED", nowMs++); break;
    case BtPlayback::Stopped: harness.line("STOPPED", nowMs++); break;
  }
  harness.line("STATUS_END", nowMs);
}

}  // namespace

int main() {
  LinkHarness harness;
  SourceManagerState source;
  harness.link.begin(0);
  assert(harness.sent.size() == 1 && harness.sent[0] == "GET_STATUS");

  provideConnectedSnapshot(harness, BtPlayback::Playing, 1);
  const size_t passiveCommandCount = harness.sent.size();
  assert(harness.link.state().runtimeAvailable);
  assert(harness.link.state().connected);
  assert(harness.link.state().playback == BtPlayback::Playing);

  // A real connection edge may select BT. A2DP PLAYING, not selection or
  // AVRCP metadata, owns the I2S route. Consuming the snapshot emits no
  // transport command back to VoxOneBT.
  SourceUpdate update = source.observe(harness.link.state());
  assert(update.reason == SourceChangeReason::BtConnect);
  assert(source.active() == ActiveSource::Bluetooth);
  BtAudioRouteTarget target = route(source, harness.link.state());
  assert(target.btOutput && !target.radioOutput && target.outputRate == 48000);
  harness.line("ARTIST Passive metadata", 20);
  harness.line("TITLE No transport side effect", 21);
  source.observe(harness.link.state());
  assert(route(source, harness.link.state()).btOutput);
  assert(harness.sent.size() == passiveCommandCount);

  // PAUSED and STOPPED are inbound A2DP state. Both close BT output without
  // synthesizing PAUSE/PLAY; returning to PLAYING reopens it in the same way.
  harness.line("PAUSED", 30);
  source.observe(harness.link.state());
  target = route(source, harness.link.state());
  assert(!target.btOutput && !target.radioOutput && target.outputRate == 0);
  harness.line("STOPPED", 31);
  source.observe(harness.link.state());
  assert(!route(source, harness.link.state()).btOutput);
  harness.line("PLAYING", 32);
  source.observe(harness.link.state());
  assert(route(source, harness.link.state()).btOutput);
  assert(harness.sent.size() == passiveCommandCount);

  // Manual source selection changes only the local route. Re-entering BT
  // preserves the peer's A2DP state and must not issue PLAY/PAUSE/NEXT/PREV.
  update = source.select(ActiveSource::Radio, harness.link.state());
  assert(update.reason == SourceChangeReason::Manual);
  target = route(source, harness.link.state());
  assert(!target.btOutput && target.radioOutput && target.outputRate == 44100);
  update = source.select(ActiveSource::Bluetooth, harness.link.state());
  assert(update.reason == SourceChangeReason::Manual);
  assert(route(source, harness.link.state()).btOutput);
  assert(harness.sent.size() == passiveCommandCount);

  // Disconnect and runtime loss fall back to RADIO without remote transport.
  harness.line("DISCONNECTED", 40);
  update = source.observe(harness.link.state());
  assert(update.reason == SourceChangeReason::BtDisconnect);
  assert(source.active() == ActiveSource::Radio);
  assert(route(source, harness.link.state()).radioOutput);
  assert(harness.sent.size() == passiveCommandCount);

  harness.line("CONNECTED", 41);
  harness.line("SAMPLE_RATE 48000", 42);
  harness.line("PLAYING", 43);
  update = source.observe(harness.link.state());
  assert(update.reason == SourceChangeReason::BtConnect);
  assert(route(source, harness.link.state()).btOutput);
  harness.link.suspend();
  update = source.observe(harness.link.state());
  assert(update.reason == SourceChangeReason::BtOffline);
  assert(source.active() == ActiveSource::Radio);
  assert(route(source, harness.link.state()).radioOutput);
  assert(harness.sent.size() == passiveCommandCount);

  // Only an explicit transport input may reach the protocol writer.
  harness.link.begin(100);
  provideConnectedSnapshot(harness, BtPlayback::Playing, 101);
  source.observe(harness.link.state());
  DisplaySourceView view{};
  source.displayView(view);
  const size_t explicitCommandStart = harness.sent.size();
  assert(btTransportAction(BtTransportInput::Toggle, view) ==
         BtTransportAction::Pause);
  assert(harness.link.pause());
  assert(btTransportAction(BtTransportInput::Next, view) ==
         BtTransportAction::Next);
  assert(harness.link.next());
  assert(btTransportAction(BtTransportInput::Previous, view) ==
         BtTransportAction::Previous);
  assert(harness.link.prev());

  harness.line("PAUSED", 120);
  source.observe(harness.link.state());
  source.displayView(view);
  assert(btTransportAction(BtTransportInput::Toggle, view) ==
         BtTransportAction::Play);
  assert(harness.link.play());
  assert(harness.sent.size() == explicitCommandStart + 4);
  assert(harness.sent[explicitCommandStart] == "PAUSE");
  assert(harness.sent[explicitCommandStart + 1] == "NEXT");
  assert(harness.sent[explicitCommandStart + 2] == "PREV");
  assert(harness.sent[explicitCommandStart + 3] == "PLAY");
}
