#include "../src/core/bt_audio_input_state.h"
#include "../src/core/bt_audio_route_state.h"
#include "../src/core/bt_link_protocol.h"
#include "../src/core/dac_mute_state.h"
#include "../src/core/mute_state.h"
#include "../src/core/source_manager_state.h"
#include "../src/core/source_vu_state.h"
#include "../src/core/temporary_audio_state.h"

#include <cassert>
#include <fstream>
#include <iterator>
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

  uint32_t connectedSnapshot(uint32_t nowMs) {
    line("STATUS_BEGIN", nowMs++);
    line("PROTO 2", nowMs++);
    line("FW_VERSION 0.6.7-dev", nowMs++);
    line("CAPS A2DP AVRCP VU", nowMs++);
    line("OTA_STATE NOT_PENDING", nowMs++);
    line("CONNECTED", nowMs++);
    line("DEVICE Audio safety phone", nowMs++);
    line("SAMPLE_RATE 48000", nowMs++);
    line("PLAYING", nowMs++);
    line("STATUS_END", nowMs++);
    return nowMs;
  }
};

struct AudioSafetySnapshot {
  BtAudioRouteTarget route{};
  DacPlaybackState dac = DacPlaybackState::Stopped;
  SourceVuResult vu{};
  bool userOutputSilent = false;
};

AudioSafetySnapshot snapshot(const SourceManagerState& source,
                             const BtLinkState& link,
                             const MuteState& mute,
                             bool radioPlaying,
                             bool temporaryActive,
                             uint32_t radioRate,
                             uint16_t radioVu,
                             uint32_t nowMs) {
  const bool btOwnsOutput = bluetoothOwnsAudio(
      source.active() == ActiveSource::Bluetooth, temporaryActive);
  const uint32_t desiredBtRate = btAudioDesiredRate(link);
  AudioSafetySnapshot result;
  result.route = btAudioRouteTarget(
      btOwnsOutput, link.playback == BtPlayback::Playing,
      desiredBtRate != 0, desiredBtRate, radioRate);
  const bool btReady = source.bluetoothAudioOutputAllowed() &&
                       link.runtimeAvailable && link.connected &&
                       desiredBtRate != 0;
  result.dac = dacPlaybackForSource(btOwnsOutput, radioPlaying, btReady,
                                    link.playback);
  uint16_t left = 0;
  uint16_t right = 0;
  uint32_t lastDataMs = 0;
  source.bluetoothRawVu(left, right, lastDataMs);
  result.vu = sourceVuSelect(
      source.active() == ActiveSource::Bluetooth, radioPlaying, radioVu,
      source.bluetoothPlaying(), left, right, lastDataMs, nowMs, 100);
  result.userOutputSilent = mute.outputSilent(42);
  return result;
}

bool sentTransport(const std::vector<std::string>& commands) {
  for (const std::string& command : commands) {
    if (command == "PLAY" || command == "PAUSE" || command == "NEXT" ||
        command == "PREV")
      return true;
  }
  return false;
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void auditProductionWiring() {
  const std::string main = readFile("src/main.cpp");
  assert(main.find("!btLink.hasIncompleteOnlineSnapshot()) sourceManagerLoop()") !=
         std::string::npos);
  assert(main.find("btAudioInput.loop(btLink.state()") != std::string::npos);
  assert(main.find("dacMute.update(dacPlayback, systemUpdateAudioBlocked())") !=
         std::string::npos);

  const std::string input = readFile("src/core/bt_audio_input.cpp");
  const size_t route = input.find("void BtAudioInput::route(");
  const size_t disable = input.find("routeEnabled_.store(false)", route);
  const size_t lock = input.find("xSemaphoreTake(outputMutex_", route);
  assert(route != std::string::npos && disable < lock);
  assert(input.find("player.outputSilent()", lock) != std::string::npos);
  assert(input.find("if (desiredRate == 0)") != std::string::npos);
  assert(input.find("route(bluetoothSelected, false, radioRate)") !=
         std::string::npos);

  const std::string manager = readFile("src/core/source_manager.cpp");
  assert(manager.find("temporaryBtLossRequiresRadioStop") !=
         std::string::npos);
  assert(manager.find("sourceState.radioActions(update, radioActive)") !=
         std::string::npos);
  assert(manager.find("player.sendCommand({PR_RADIO_RESUME, resumeStation})") !=
         std::string::npos);
}

}  // namespace

int main() {
  auditProductionWiring();

  LinkHarness harness;
  SourceManagerState source;
  MuteState mute;
  harness.link.begin(0);
  uint32_t nowMs = harness.connectedSnapshot(10);
  SourceUpdate update = source.observe(harness.link.state());
  assert(update.reason == SourceChangeReason::BtConnect);
  harness.line("VU 16000 8000", nowMs++);
  source.observe(harness.link.state());

  AudioSafetySnapshot state = snapshot(
      source, harness.link.state(), mute, false, false, 44100, 0xffff, nowMs);
  assert(state.route.btOutput && !state.route.radioOutput);
  assert(state.route.outputRate == 48000);
  assert(state.dac == DacPlaybackState::Playing &&
         dacXsmtHigh(state.dac, false));
  assert(state.vu.playing && state.vu.levels != 0);
  assert(!state.userOutputSilent);

  // User MUTE persists independently: BT routing may remain selected, but the
  // actual PCM write path is zeroed by Player::outputSilent().
  mute.set(true);
  state = snapshot(source, harness.link.state(), mute, false, false, 44100,
                   0xffff, nowMs);
  assert(state.route.btOutput && state.userOutputSilent && mute.active());

  // A2DP STOP closes BT output, lowers logical XSMT and clears VU without
  // selecting RADIO or issuing an outgoing transport command.
  harness.line("STOPPED", nowMs++);
  update = source.observe(harness.link.state());
  assert(update.titleChanged && !update.activeChanged);
  state = snapshot(source, harness.link.state(), mute, false, false, 44100,
                   0xffff, nowMs);
  assert(!state.route.btOutput && !state.route.radioOutput &&
         state.route.outputRate == 0);
  assert(state.dac == DacPlaybackState::Stopped &&
         !dacXsmtHigh(state.dac, false));
  assert(!state.vu.playing && state.vu.levels == 0);
  harness.line("VU 32768 32768", nowMs++);  // late sample is ignored
  harness.line("STOPPED", nowMs++);         // duplicate is idempotent
  update = source.observe(harness.link.state());
  assert(!update.activeChanged && !update.titleChanged);

  // A new PLAYING state does not reuse the late VU sample. Only a new sample
  // reopens the meter; no local command is sent to the peer.
  harness.line("PLAYING", nowMs++);
  update = source.observe(harness.link.state());
  assert(update.titleChanged);
  state = snapshot(source, harness.link.state(), mute, false, false, 44100,
                   0xffff, nowMs);
  assert(state.route.btOutput && state.vu.playing && state.vu.levels == 0);
  harness.line("VU 9000 4000", nowMs++);
  source.observe(harness.link.state());
  state = snapshot(source, harness.link.state(), mute, false, false, 44100,
                   0xffff, nowMs);
  assert(state.vu.levels != 0);

  // A2DP disconnect falls back to RADIO STOP, disables the BT path, clears
  // VU, lowers XSMT and retains user MUTE. Late PLAYING/VU cannot reopen BT.
  harness.line("DISCONNECTED", nowMs++);
  update = source.observe(harness.link.state());
  assert(update.reason == SourceChangeReason::BtDisconnect);
  assert(!source.radioActions(update, false).resume);
  state = snapshot(source, harness.link.state(), mute, false, false, 44100,
                   0xffff, nowMs);
  assert(source.active() == ActiveSource::Radio);
  assert(!state.route.btOutput && state.route.radioOutput);
  assert(state.dac == DacPlaybackState::Stopped &&
         !dacXsmtHigh(state.dac, false));
  assert(!state.vu.playing && state.vu.levels == 0 && mute.active());
  harness.line("PLAYING", nowMs++);
  harness.line("VU 32768 32768", nowMs++);
  update = source.observe(harness.link.state());
  assert(!update.activeChanged && source.active() == ActiveSource::Radio);
  state = snapshot(source, harness.link.state(), mute, false, false, 44100,
                   0xffff, nowMs);
  assert(!state.route.btOutput && state.dac == DacPlaybackState::Stopped);
  harness.line("DISCONNECTED", nowMs++);
  update = source.observe(harness.link.state());
  assert(!update.activeChanged && !update.btDisconnected);

  // A real reconnect edge may select BT again, but does not synthesize PLAY.
  harness.line("CONNECTED", nowMs++);
  harness.line("SAMPLE_RATE 48000", nowMs++);
  harness.line("PLAYING", nowMs++);
  update = source.observe(harness.link.state());
  assert(update.reason == SourceChangeReason::BtConnect);
  state = snapshot(source, harness.link.state(), mute, false, false, 44100,
                   0xffff, nowMs);
  assert(state.route.btOutput && state.userOutputSilent && mute.active());

  // Missing heartbeat/status eventually invalidates the whole session. The
  // exact timeout edge is fail-closed and repeated ticks remain idempotent.
  const uint32_t lastRxMs = nowMs - 1;
  harness.link.tick(lastRxMs + BtLinkProtocol::OfflineTimeoutMs - 1);
  assert(harness.link.state().runtimeAvailable);
  harness.link.tick(lastRxMs + BtLinkProtocol::OfflineTimeoutMs);
  assert(!harness.link.state().runtimeAvailable);
  update = source.observe(harness.link.state());
  assert(update.reason == SourceChangeReason::BtOffline);
  assert(!source.radioActions(update, false).resume);
  state = snapshot(source, harness.link.state(), mute, false, false, 44100,
                   0xffff, lastRxMs + BtLinkProtocol::OfflineTimeoutMs);
  assert(!state.route.btOutput && state.route.radioOutput);
  assert(state.dac == DacPlaybackState::Stopped &&
         !dacXsmtHigh(state.dac, false));
  assert(state.vu.levels == 0 && !state.vu.playing);
  harness.link.tick(lastRxMs + BtLinkProtocol::OfflineTimeoutMs + 1);
  update = source.observe(harness.link.state());
  assert(!update.activeChanged && source.active() == ActiveSource::Radio);

  // Delayed standalone UART lines cannot revive an offline module. Only a
  // complete status snapshot creates a new online/connection edge.
  harness.line("CONNECTED", nowMs + 20000);
  harness.line("PLAYING", nowMs + 20001);
  harness.line("VU 32768 32768", nowMs + 20002);
  assert(!harness.link.state().runtimeAvailable);
  update = source.observe(harness.link.state());
  assert(!update.activeChanged && source.active() == ActiveSource::Radio);
  nowMs = harness.connectedSnapshot(nowMs + 20010);
  update = source.observe(harness.link.state());
  assert(update.reason == SourceChangeReason::BtConnect);
  assert(source.active() == ActiveSource::Bluetooth);

  // If no audio source is available, a zero RADIO rate represents no-source:
  // neither route is enabled and logical XSMT remains low.
  source.select(ActiveSource::Radio, harness.link.state());
  state = snapshot(source, harness.link.state(), mute, false, false, 0, 0,
                   nowMs);
  assert(!state.route.radioOutput && !state.route.btOutput &&
         state.route.outputRate == 0);
  assert(state.dac == DacPlaybackState::Stopped &&
         !dacXsmtHigh(state.dac, false));

  // RADIO PLAY/STOP and the one-shot TTS suppression keep their established
  // semantics while BT disappears.
  SourceManagerState ttsSource;
  TemporaryAudioState temporary;
  ttsSource.recordRadioCommand(true, 7);
  BtLinkState ttsLink = harness.link.state();
  assert(ttsSource.observe(ttsLink).reason == SourceChangeReason::BtConnect);
  const uint32_t token = temporary.begin();
  ttsLink.connected = false;
  ttsLink.playback = BtPlayback::Stopped;
  update = ttsSource.observe(ttsLink);
  assert(temporaryBtLossRequiresRadioStop(update, temporary.busy()));
  assert(temporary.suppressRadioRestore());
  assert(temporary.signal(token) && temporary.finish(token));
  assert(!temporary.takeRadioRestore(ttsSource.radioResumeAllowed(), true,
                                     false));
  assert(ttsSource.radioPlayIntent());
  assert(!temporary.takeRadioRestore(true, true, false));

  assert(!sentTransport(harness.sent));
}
