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

  // Exercise the production termination contract as well as the state model.
  // Decoder callbacks are synchronous inside Audio::loop(), so the callback
  // token belongs to exactly that invocation. A backend timeout is also safe
  // when it only stops the decoder: Player observes !isRunning() and finishes
  // the same temporary generation without requiring a callback.
  const auto handlers = readFile("src/core/audiohandlers.h");
  assert(handlers.find("void audio_error(const char *info) {") !=
         std::string::npos);
  assert(handlers.find("player.setError(info);") != std::string::npos);
  assert(handlers.find("if (player.temporaryEof()) return;") !=
         std::string::npos);
  assert(handlers.find("player.temporaryEof();") != std::string::npos);
  assert(player.find("_temporary.signal(_callbackToken);") !=
         std::string::npos);
  assert(player.find("_callbackToken = _temporary.token();") !=
         std::string::npos);
  assert(player.find("if (!_temporary.terminal()) Audio::loop();") !=
         std::string::npos);
  assert(player.find(
      "if (_temporary.terminal() || !isRunning()) finishTemporary();") !=
      std::string::npos);
  assert(player.find("const bool connected = connecttohost(url);") !=
         std::string::npos);
  assert(player.find("_temporary.signal(token);") != std::string::npos);
  assert(player.find("xQueueOverwrite(_temporaryUrls, request);") !=
         std::string::npos);
  assert(player.find("if (temporaryActive()) finishTemporary();") !=
         std::string::npos);

  // The only production ingress for temporary audio is the existing MQTT
  // command topic. Long payloads are forwarded as URL bytes to Player; the
  // negative-path HIL therefore exercises the same browseUrl() flow as a
  // normal announcement and needs no diagnostic endpoint.
  const auto mqtt = readFile("src/core/mqtt.cpp");
  assert(mqtt.find("player.requestTemporaryUrl(payload, len);") !=
         std::string::npos);
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
  void backendStopped(uint32_t token) {
    assert(!temporary.terminal());
    assert(temporary.finish(token));
    restore();
  }
};
}

int main() {
  auditTransport();
  // Update STOP cancels an active announcement without changing smartstart
  // or restoring the previous RADIO intent after the lock is released.
  {
    Scenario s;
    s.radio(true);
    const auto token = s.temporary.begin();
    assert(s.temporary.suppressRadioRestore());
    s.source.stopForUpdate(s.bt);
    assert(!radioStopUpdatesSmartStart(RadioStopReason::SourceSwitch, false));
    assert(s.temporary.finish(token));
    s.restore(true, true);
    s.restore();
    assert(s.resumes == 0 && !s.source.radioPlayIntent() && !s.temporary.busy());
  }
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

  // EOF, decoder/stream error and connect failure signal the callback token.
  // Duplicate notifications and an old token cannot finish a replacement.
  for (int terminalPath = 0; terminalPath < 3; ++terminalPath) {
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

  // A transport/header timeout may stop the backend without emitting an
  // error callback. Player's !isRunning() fallback still completes exactly
  // once, and a late callback for that generation is harmless.
  {
    Scenario timeout;
    timeout.radio(true);
    const auto token = timeout.temporary.begin();
    timeout.backendStopped(token);
    assert(timeout.resumes == 1 && !timeout.temporary.busy());
    assert(!timeout.temporary.signal(token));
    assert(!timeout.temporary.finish(token));
    timeout.restore();
    assert(timeout.resumes == 1);
  }

  // RC1 negative-path matrix. Production decoder/connect errors signal the
  // active callback token, while a no-data transport timeout is observed as
  // a stopped backend. Both converge on the same one-shot cleanup/restore.

  // A/B: RADIO PLAY restores exactly once after error; RADIO STOP remains
  // stopped and cannot gain a synthetic PLAY intent.
  for (bool playing : {true, false}) {
    Scenario error;
    error.radio(playing);
    const auto token = error.temporary.begin();
    error.end(token);  // audio_error/connect failure -> signaled terminal.
    assert(!error.temporary.busy());
    assert(error.resumes == (playing ? 1U : 0U));
    assert(error.source.radioPlayIntent() == playing);
    error.restore();
    assert(error.resumes == (playing ? 1U : 0U));
  }

  // C: an error during BT PLAY returns ownership to the still-playing BT
  // observation without issuing transport commands or restoring RADIO.
  {
    Scenario error;
    error.radio(true);
    error.connect(BtPlayback::Playing);
    const auto token = error.temporary.begin();
    error.end(token);
    assert(!error.temporary.busy() && error.resumes == 0);
    assert(error.source.active() == ActiveSource::Bluetooth);
    assert(error.bt.playback == BtPlayback::Playing);
    assert(error.route().btOutput && !error.route().radioOutput);
  }

  // D: a timeout over BT PAUSED/STOPPED preserves that observed state and
  // never synthesizes RESUME or a RADIO restore.
  for (const auto playback : {BtPlayback::Paused, BtPlayback::Stopped}) {
    Scenario timeout;
    timeout.radio(true);
    timeout.connect(playback);
    const auto token = timeout.temporary.begin();
    timeout.backendStopped(token);
    assert(!timeout.temporary.busy() && timeout.resumes == 0);
    assert(timeout.source.active() == ActiveSource::Bluetooth);
    assert(timeout.bt.playback == playback);
    assert(!timeout.route().btOutput && !timeout.route().radioOutput);
    timeout.restore();
    assert(timeout.resumes == 0);
  }

  // F: source selection at the timeout boundary is authoritative. Neither
  // interleaving restores the source that was active when TTS began.
  {
    Scenario toBluetooth;
    toBluetooth.radio(true);
    toBluetooth.bt.runtimeAvailable = true;
    toBluetooth.bt.connected = true;
    toBluetooth.bt.playback = BtPlayback::Playing;
    toBluetooth.bt.sampleRate = 44100;
    auto token = toBluetooth.temporary.begin();
    assert(toBluetooth.source.select(ActiveSource::Bluetooth, toBluetooth.bt)
               .activeChanged);
    toBluetooth.backendStopped(token);
    assert(toBluetooth.source.active() == ActiveSource::Bluetooth);
    assert(toBluetooth.resumes == 0 && toBluetooth.route().btOutput);

    Scenario toRadio;
    toRadio.radio(true);
    toRadio.connect(BtPlayback::Playing);
    token = toRadio.temporary.begin();
    assert(toRadio.source.select(ActiveSource::Radio, toRadio.bt).activeChanged);
    toRadio.backendStopped(token);
    assert(toRadio.source.active() == ActiveSource::Radio);
    assert(toRadio.resumes == 1 && !toRadio.temporary.busy());
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
  assert(!radioWifiReconnectShouldPlay(lostPlaying, true, true));
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
