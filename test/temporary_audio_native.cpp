#include "../src/core/temporary_audio_state.h"
#include "../src/core/source_manager_state.h"
#include "../src/core/bt_audio_route_state.h"
#include "../src/core/radio_source_policy.h"
#include "../src/core/dac_mute_state.h"

#include <cassert>
#include <initializer_list>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void auditTransport() {
  // Check production wiring, not a synthetic command counter: the temporary
  // owner/Player have no BT command path. All four transport calls remain in
  // the explicit user-input dispatcher.
  const auto player = readFile("src/core/player.cpp");
  assert(player.find("btLink.") == std::string::npos);
  assert(player.find("sourceManagerTransport(") == std::string::npos);
  assert(player.find("resumeAfterUrl") == std::string::npos);
  const auto source = readFile("src/core/source_manager.cpp");
  assert(source.find(
      "temporaryBtLossRequiresRadioStop(update, temporaryAtChange)") !=
      std::string::npos);
  assert(source.find("player.suppressTemporaryRadioRestore()") !=
         std::string::npos);
  const auto begin = source.find("void sourceManagerTransport(");
  const auto end = source.find("void cycleNextSource(", begin);
  assert(begin != std::string::npos && end != std::string::npos);
  for (const char* command : {"btLink.play()", "btLink.pause()", "btLink.next()",
                              "btLink.prev()", "btLink.stop()"}) {
    size_t pos = 0;
    while ((pos = source.find(command, pos)) != std::string::npos) {
      assert(pos > begin && pos < end);
      ++pos;
    }
  }
}

struct Scenario {
  SourceManagerState source;
  TemporaryAudioState temporary;
  BtLinkState bt{};
  uint16_t station = 17;
  unsigned resumes = 0;

  void radio(bool play, uint16_t selected = 17) {
    source.recordRadioCommand(play, selected);
  }
  void connect(BtPlayback playback) {
    bt.runtimeAvailable = bt.connected = true;
    bt.playback = playback;
    bt.sampleRate = 44100;
    source.observe(bt);
  }
  BtAudioRouteTarget route() const {
    return btAudioRouteTarget(
        bluetoothOwnsAudio(source.active() == ActiveSource::Bluetooth,
                           temporary.active()),
        bt.playback == BtPlayback::Playing, bt.runtimeAvailable && bt.connected,
        bt.sampleRate, 24000);
  }
  void restore(bool online = true, bool updating = false) {
    if (temporary.takeRadioRestore(source.radioResumeAllowed(), online, updating)) {
      station = source.radioStationForResume(station);
      source.radioPlayConsumed();
      ++resumes;
    }
  }
  void end(uint32_t token) {
    temporary.signal(token);
    assert(temporary.terminal());
    assert(temporary.finish(token));
    restore();
  }
};
}

int main() {
  auditTransport();
  // RADIO PLAY/STOP: TTS never becomes selected source or radio PLAY intent.
  for (bool playing : {false, true}) {
    Scenario s;
    s.radio(playing);
    const auto token = s.temporary.begin();
    assert(s.source.active() == ActiveSource::Radio);
    assert(s.route().radioOutput && !s.route().btOutput);
    assert(s.source.radioPlayIntent() == playing);
    assert(dacXsmtHigh(dacPlaybackForSource(false, true, false,
                                          BtPlayback::Paused), false));
    assert(!radioStopUpdatesSmartStart(RadioStopReason::SourceSwitch, false));
    assert(!radioPlayPreparationUpdatesSmartStart(true, 1));
    s.end(token);
    assert(!s.temporary.busy());
    assert(s.resumes == (playing ? 1U : 0U) && s.station == 17);
    s.restore();
    assert(s.resumes == (playing ? 1U : 0U));
  }

  // BT playback/connection are observations, never commands from override.
  for (auto playback : {BtPlayback::Playing, BtPlayback::Paused}) {
    Scenario s;
    s.radio(true);
    s.connect(playback);
    const auto token = s.temporary.begin();
    assert(s.source.active() == ActiveSource::Bluetooth);
    assert(!s.route().btOutput && s.route().radioOutput);
    assert(s.route().outputRate == 24000);
    assert(dacXsmtHigh(dacPlaybackForSource(
        bluetoothOwnsAudio(true, s.temporary.active()), true, true, playback), false));
    s.end(token);
    assert(s.resumes == 0 && s.bt.connected && s.bt.playback == playback);
    assert(!s.route().radioOutput);
    assert(s.route().btOutput == (playback == BtPlayback::Playing));
    assert(s.route().outputRate == (playback == BtPlayback::Playing ? 44100U : 0U));
    assert(dacXsmtHigh(dacPlaybackForSource(true, false, true, playback), false) ==
           (playback == BtPlayback::Playing));
  }

  // Disconnect/offline during URL changes base, but cannot terminate URL.
  // This one fallback intentionally consumes the restore without autoplay,
  // even when the earlier RADIO intent was PLAY.
  for (bool playing : {false, true}) {
    for (bool offline : {false, true}) {
      Scenario s;
      s.radio(playing);
      s.connect(BtPlayback::Playing);
      const auto token = s.temporary.begin();
      if (offline) s.bt.runtimeAvailable = false;
      else s.bt.connected = false;
      const SourceUpdate update = s.source.observe(s.bt);
      assert(temporaryBtLossRequiresRadioStop(update, s.temporary.busy()));
      assert(s.temporary.suppressRadioRestore());
      assert(s.source.active() == ActiveSource::Radio);
      assert(s.temporary.active() && !s.temporary.terminal());
      assert(s.route().radioOutput);
      s.end(token);
      assert(s.resumes == 0);
      assert(s.source.radioPlayIntent() == playing);
      assert(!s.temporary.busy());

      // The exception is one-shot; a later ordinary TTS still follows the
      // preserved current RADIO intent.
      const auto next = s.temporary.begin();
      s.end(next);
      assert(s.resumes == (playing ? 1U : 0U));
    }
  }

  // Replacing the active URL does not lose an already observed BT-loss stop.
  Scenario replacement;
  replacement.radio(true);
  replacement.connect(BtPlayback::Playing);
  const auto replaced = replacement.temporary.begin();
  replacement.bt.connected = false;
  SourceUpdate loss = replacement.source.observe(replacement.bt);
  assert(temporaryBtLossRequiresRadioStop(loss, replacement.temporary.busy()));
  assert(replacement.temporary.suppressRadioRestore());
  const auto replacementToken = replacement.temporary.begin();
  assert(replaced != replacementToken);
  replacement.end(replacementToken);
  assert(replacement.resumes == 0 && replacement.source.radioPlayIntent());

  SourceUpdate noChange;
  assert(!temporaryBtLossRequiresRadioStop(noChange, true));
  noChange.activeChanged = true;
  noChange.reason = SourceChangeReason::Manual;
  assert(!temporaryBtLossRequiresRadioStop(noChange, true));
  noChange.reason = SourceChangeReason::BtDisconnect;
  assert(!temporaryBtLossRequiresRadioStop(noChange, false));

  // EOF, decoder/stream error, connect failure and timeout share the terminal
  // signal. Duplicate notifications and an old token cannot finish a new URL.
  for (int terminalPath = 0; terminalPath < 4; ++terminalPath) {
    Scenario s;
    s.radio(true);
    const auto first = s.temporary.begin();
    assert(s.temporary.signal(first));
    const auto second = s.temporary.begin();
    assert(first != second && !s.temporary.terminal());
    assert(!s.temporary.signal(first));
    assert(!s.temporary.finish(first));
    assert(s.temporary.token() == second);
    s.end(second);
    assert(!s.temporary.signal(second));
    assert(!s.temporary.finish(second));
    assert(s.resumes == 1 && s.station == 17);
    const auto third = s.temporary.begin();
    assert(!s.temporary.signal(second));
    s.end(third);
    assert(s.resumes == 2);
  }

  // Manual selection during TTS wins over the source at announcement start.
  Scenario manual;
  manual.radio(true);
  manual.bt.runtimeAvailable = true;
  auto token = manual.temporary.begin();
  manual.source.select(ActiveSource::Bluetooth, manual.bt);
  manual.end(token);
  assert(manual.source.active() == ActiveSource::Bluetooth && manual.resumes == 0);
  token = manual.temporary.begin();
  manual.source.select(ActiveSource::Radio, manual.bt);
  manual.end(token);
  assert(manual.source.active() == ActiveSource::Radio && manual.resumes == 1);

  // Explicit RADIO STOP/PLAY changes current intent, including station choice.
  Scenario commands;
  commands.radio(true);
  token = commands.temporary.begin();
  commands.radio(false);
  commands.end(token);
  assert(commands.resumes == 0 && !commands.source.radioPlayIntent());
  token = commands.temporary.begin();
  commands.radio(true, 29);
  commands.end(token);
  assert(commands.resumes == 1 && commands.station == 29);

  // Wi-Fi loss: one owner of recovery, wait for connectivity without duration
  // limit. A reconnect callback may not queue its separate lostPlaying resume.
  Scenario wifi;
  wifi.radio(true);
  token = wifi.temporary.begin();
  bool lostPlaying = !wifi.temporary.busy();
  assert(!lostPlaying);
  assert(wifi.temporary.signal(token));
  assert(wifi.temporary.finish(token));
  for (int i = 0; i < 100; ++i) wifi.restore(false);
  assert(wifi.resumes == 0 && wifi.temporary.busy());
  assert(!radioWifiReconnectShouldPlay(lostPlaying, true));
  wifi.restore(true);
  wifi.restore(true);
  assert(wifi.resumes == 1 && !wifi.temporary.busy());
  // Intent can also change while waiting for Wi-Fi.
  token = wifi.temporary.begin();
  wifi.temporary.finish(token);
  wifi.radio(false);
  wifi.restore(false);
  wifi.restore(true);
  assert(wifi.resumes == 1 && !wifi.temporary.busy());

  // Update cancellation must never restore audio.
  Scenario update;
  update.radio(true);
  token = update.temporary.begin();
  update.temporary.finish(token);
  update.restore(true, true);
  update.restore();
  assert(update.resumes == 0 && !update.temporary.busy());
}
