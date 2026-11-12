#include "../src/core/temporary_audio_state.h"
#include "../src/core/temporary_audio_url.h"
#include "../src/core/temporary_audio_request.h"
#include "../src/core/temporary_transport_watchdog.h"
#include "../src/core/source_manager_state.h"
#include "../src/core/bt_audio_route_state.h"
#include "../src/core/radio_source_policy.h"
#include "../src/core/dac_mute_state.h"
#include "../src/core/web_status_view.h"
#include "../src/core/player_display_view.h"
#include "../src/audioI2S/web_file_bounds.h"

#include <cassert>
#include <cstdio>
#include <cstring>
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

size_t countOccurrences(const std::string& value, const std::string& needle) {
  size_t count = 0;
  size_t position = 0;
  while ((position = value.find(needle, position)) != std::string::npos) {
    ++count;
    position += needle.size();
  }
  return count;
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
  const auto end = source.find(
      "\n#endif\n\nbool sourceManagerSelectSource(", begin);
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
  assert(player.find("if (radioMetadataAllowed()) {") != std::string::npos);
  assert(player.find("_stop(false, RadioStopReason::SourceSwitch, true);") !=
         std::string::npos);
  assert(player.find("_temporary.allowRadioMetadata();") !=
         std::string::npos);
  assert(player.find("_callbackToken = _temporary.token();") !=
         std::string::npos);
  assert(player.find("if (!_temporary.terminal()) Audio::loop();") !=
         std::string::npos);
  assert(player.find("serviceTemporaryAudioTransport(") != std::string::npos);
  assert(player.find("transportProgressBytes()") != std::string::npos);
  assert(player.find("transportConnected()") != std::string::npos);
  assert(player.find("transportBodyComplete()") != std::string::npos);
  assert(player.find("transportBufferedBytes()") != std::string::npos);
  assert(player.find("transportBufferedFrameAvailable()") !=
         std::string::npos);
  assert(player.find("transport disconnected before EOF") !=
         std::string::npos);
  assert(player.find("const bool connected = connecttohost(request.url);") !=
         std::string::npos);
  assert(player.find("_temporary.signal(token);") != std::string::npos);
  assert(player.find("xQueueCreate(1, sizeof(QueuedTemporaryRequest*))") !=
         std::string::npos);
  assert(player.find("xQueueSend(_temporaryUrls, &pending, 0)") !=
         std::string::npos);
  assert(player.find("releaseTemporaryRequest(replaced)") !=
         std::string::npos);
  assert(player.find("request.type == PR_STOP && temporaryBusy()") !=
         std::string::npos);
  assert(player.find("request.type == PR_STOP && !stopsTemporary") !=
         std::string::npos);
  assert(player.find("if (temporaryActive()) {") !=
         std::string::npos);
  const auto browse = player.find("void Player::browseUrl(");
  const auto browseEnd = player.find("void Player::prev()", browse);
  assert(browse != std::string::npos && browseEnd != std::string::npos);
  const auto browseBody = player.substr(browse, browseEnd - browse);
  assert(browseBody.find("config.setTitle(") == std::string::npos);
  const auto progressBegin = browseBody.find("_temporaryProgress.begin(");
  const auto connectionBegin = browseBody.find("connecttohost(request.url)");
  assert(progressBegin != std::string::npos &&
         connectionBegin != std::string::npos &&
         progressBegin < connectionBegin);
  const auto finish = player.find("void Player::finishTemporary()");
  const auto restore = player.find("void Player::restoreTemporaryBase()", finish);
  assert(finish != std::string::npos && restore != std::string::npos);
  const auto finishBody = player.substr(finish, restore - finish);
  assert(finishBody.find("requestOnChange(MODE, 0)") != std::string::npos);
  assert(finishBody.find("requestOnChange(WEBSTATUS, 0)") != std::string::npos);
  assert(finishBody.find("display.putRequest(NEWSTATION)") != std::string::npos);
  assert(finishBody.find("display.putRequest(NEWTITLE)") != std::string::npos);
  assert(finishBody.find("_temporaryProgress.finish(token)") !=
         std::string::npos);
  assert(finishBody.find("sourceManagerEndTemporary()") !=
         std::string::npos);
  assert(browseBody.find("sourceManagerBeginHomeAssistant(") !=
         std::string::npos);
  assert(browseBody.find("sourceManagerBeginDlna(") !=
         std::string::npos);

  // Every HTTP consumer reports real bytes through one Audio-owned counter.
  // Player observes that counter only while temporary audio is active, so the
  // normal RADIO reconnect/timeout policy is not changed.
  const auto audio = readFile("src/audioI2S/Audio.cpp");
  const auto audioHeader = readFile("src/audioI2S/AudioEx.h");
  assert(countOccurrences(audio, "_client->read(") == 2);
  assert(countOccurrences(audio, "_client->readBytes(") == 1);
  assert(audio.find("m_transportProgressBytes +=") != std::string::npos);
  assert(audioHeader.find("uint64_t transportProgressBytes() const") !=
         std::string::npos);
  assert(audioHeader.find("bool transportBodyComplete() const") !=
         std::string::npos);
  assert(audio.find("m_webFileDataComplete = true;") != std::string::npos);
  const auto webFile = audio.find("void Audio::processWebFile()");
  const auto bodyComplete = audio.find(
      "m_webFileDataComplete = true;", webFile);
  const auto waitForStream = audio.find("if(!f_stream) return;", webFile);
  assert(webFile != std::string::npos && bodyComplete != std::string::npos &&
         waitForStream != std::string::npos && bodyComplete < waitForStream);
  assert(audio.find(
      "InBuff.bufferFilled() > maxFrameSize || m_webFileDataComplete",
      webFile) != std::string::npos);
  assert(audioHeader.find("bool transportBufferedFrameAvailable()") !=
         std::string::npos);

  // Every decoder path capable of mutating the shared RADIO presentation is
  // behind the production metadata gate. An empty showstation callback is a
  // no-op, not a self-copy that clears config.station.name.
  size_t guardedCallbacks = 0;
  size_t guard = 0;
  while ((guard = handlers.find("if (!player.radioMetadataAllowed()) return;",
                                guard)) != std::string::npos) {
    ++guardedCallbacks;
    ++guard;
  }
  assert(guardedCallbacks >= 6);
  assert(handlers.find("config.setStation(p?info:config.station.name)") ==
         std::string::npos);

  // HA keeps the authenticated WebSocket command. DLNA additionally exposes
  // one protected REST playback endpoint carrying an explicit DLNA source.
  const auto server = readFile("src/core/netserver.cpp");
  assert(server.find("dlna::kPlayRoute") != std::string::npos);
  assert(server.find("Source::Dlna") != std::string::npos);
  assert(server.find("playback.view(voxone::temporary_audio::Source::Dlna)") !=
         std::string::npos);
  const auto commands = readFile("src/core/commandhandler.cpp");
  assert(commands.find("strEquals(command, \"playurl\")") !=
         std::string::npos);
  assert(commands.find("player.requestTemporaryUrl(request);") !=
         std::string::npos);
  assert(commands.find("parseRequest(value, length, storage)") !=
         std::string::npos);
}

void validateTemporaryUrls() {
  using voxone::temporary_audio::isSafeUrl;
  using voxone::temporary_audio::kUrlCapacity;
  const char* http = "http://192.168.1.10:8123/api/tts_proxy/file.mp3?token=a=b";
  const char* https = "https://ha.local/media/audio.mp3#fragment";
  assert(isSafeUrl(http, std::strlen(http)));
  assert(isSafeUrl(https, std::strlen(https)));
  assert(!isSafeUrl(nullptr, 0));
  assert(!isSafeUrl("", 0));
  const char* ftp = "ftp://ha.local/file.mp3";
  const char* missingHost = "http:///file.mp3";
  const char* credentials = "http://user@ha.local/file.mp3";
  const char* space = "http://ha.local/file mp3";
  const char* crlf = "http://ha.local/file\r\nX:1";
  assert(!isSafeUrl(ftp, std::strlen(ftp)));
  assert(!isSafeUrl(missingHost, std::strlen(missingHost)));
  assert(!isSafeUrl(credentials, std::strlen(credentials)));
  assert(!isSafeUrl(space, std::strlen(space)));
  assert(!isSafeUrl(crlf, std::strlen(crlf)));
  const std::string tooLong(kUrlCapacity, 'a');
  assert(!isSafeUrl(tooLong.c_str(), tooLong.size()));

  static_assert(sizeof(voxone::temporary_audio::Request) <=
                    sizeof(void*) * 4,
                "temporary request view must stay pointer-sized");
  static_assert(sizeof(voxone::temporary_audio::RequestStorage) >
                    sizeof(voxone::temporary_audio::Request),
                "bounded parsing storage must not enter the mailbox");
  voxone::temporary_audio::RequestStorage storage{};
  const char* withMetadata =
      "http://ha.local/api/tts.mp3?token=a=b&title=Home%20Assistant"
      "&artist=Drzwi%20s%C4%85%20otwarte";
  assert(voxone::temporary_audio::parseRequest(
      withMetadata, std::strlen(withMetadata), storage));
  auto request = storage.view();
  assert(std::strcmp(request.url,
                     "http://ha.local/api/tts.mp3?token=a=b") == 0);
  assert(std::strcmp(request.title, "Home Assistant") == 0);
  assert(std::strcmp(request.artist, "Drzwi są otwarte") == 0);
  assert(request.source == voxone::temporary_audio::Source::HomeAssistant);
  assert(voxone::temporary_audio::parseRequest(
      http, std::strlen(http), storage));
  request = storage.view();
  assert(std::strcmp(request.url, http) == 0);
  assert(std::strcmp(request.title, "Home Assistant") == 0);
  assert(request.artist[0] == '\0');
  const char* malformed =
      "http://ha.local/a.mp3&title=Home%2&artist=TTS";
  assert(!voxone::temporary_audio::parseRequest(
      malformed, std::strlen(malformed), storage));
}

struct Scenario {
  SourceManagerState source;
  TemporaryAudioState temporary;
  BtLinkState bt{};
  uint16_t station = 17;
  unsigned resumes = 0;
  unsigned cleanups = 0;
  bool radioRunning = false;
  char stationName[64] = "Radio test";
  char metadata[96] = "Stan bazowy";

  void radio(bool play, uint16_t selected = 17) {
    source.recordRadioCommand(play, selected);
    radioRunning = play;
  }
  void connect(BtPlayback playback) {
    bt.runtimeAvailable = bt.connected = true;
    bt.playback = playback;
    bt.sampleRate = 44100;
    std::snprintf(bt.peerName, sizeof(bt.peerName), "%s", "Telefon testowy");
    std::snprintf(bt.artist, sizeof(bt.artist), "%s", "BT artysta");
    std::snprintf(bt.title, sizeof(bt.title), "%s", "BT utwor");
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
      temporary.allowRadioMetadata();
      radioRunning = true;
      ++resumes;
    }
  }
  void end(uint32_t token) {
    temporary.signal(token);
    assert(temporary.terminal());
    radioRunning = false;
    assert(temporary.finish(token));
    ++cleanups;
    restore();
  }
  void backendStopped(uint32_t token) {
    assert(!temporary.terminal());
    radioRunning = false;
    assert(temporary.finish(token));
    ++cleanups;
    restore();
  }
  bool decoderMetadata(uint32_t callbackToken, const char* stationValue,
                       const char* metadataValue) {
    if (!temporary.radioMetadataAllowed(callbackToken)) return false;
    if (stationValue)
      std::snprintf(stationName, sizeof(stationName), "%s", stationValue);
    if (metadataValue)
      std::snprintf(metadata, sizeof(metadata), "%s", metadataValue);
    return true;
  }
  DisplaySourceView sourceView() const {
    DisplaySourceView view{};
    source.displayView(view, radioRunning, true);
    return view;
  }
  WebStatusView webView() const {
    const DisplaySourceView view = sourceView();
    return selectWebStatusView(view, stationName, metadata, "", "", "MP3", 0);
  }
  PlayerDisplayView displayView() const {
    const DisplaySourceView view = sourceView();
    PlayerDisplayView display{};
    makePlayerDisplayView(display, view, stationName, metadata, false,
                          37, false, bt.connected, -55, 0, BF_UNKNOWN);
    return display;
  }
};

struct TransportScenario : Scenario {
  voxone::temporary_audio::ProgressWatchdog progress;
  unsigned timeouts = 0;
  unsigned disconnects = 0;

  uint32_t beginTransport(uint32_t nowMs, uint64_t progressBytes) {
    const uint32_t token = temporary.begin();
    progress.begin(token, nowMs, progressBytes);
    return token;
  }

  bool service(uint32_t nowMs, uint64_t progressBytes,
               bool backendRunning = true, bool connected = true,
               bool bufferedFrameAvailable = false,
               bool bodyComplete = false, uint32_t bufferedBytes = 0) {
    return voxone::temporary_audio::serviceTemporaryAudioTransport(
        temporary, progress, nowMs, progressBytes, backendRunning, connected,
        bodyComplete, bufferedBytes, bufferedFrameAvailable,
        [this](uint32_t) { ++timeouts; },
        [this](uint32_t) { ++disconnects; },
        [this]() {
          const uint32_t token = temporary.token();
          radioRunning = false;
          assert(temporary.finish(token));
          progress.finish(token);
          ++cleanups;
          restore();
        });
  }
};

void assertRadioPresentation(const Scenario& scenario, bool playing) {
  const WebStatusView web = scenario.webView();
  assert(std::strcmp(web.activeSource, "radio") == 0);
  assert(std::strcmp(web.name, "Radio test") == 0);
  assert(std::strcmp(web.metadata, "Stan bazowy") == 0);
  assert(std::strcmp(web.transport, playing ? "playing" : "stopped") == 0);
  const PlayerDisplayView display = scenario.displayView();
  assert(display.source == DisplaySourceKind::Radio);
  assert(display.playback == (playing ? DisplayPlaybackState::Playing
                                     : DisplayPlaybackState::Stopped));
  assert(std::strstr(display.artist, "HTTP/") == nullptr);
  assert(std::strstr(display.title, "HTTP/") == nullptr);
}
}

int main() {
  auditTransport();
  validateTemporaryUrls();
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
        bluetoothOwnsAudio(true, s.temporary.active()), true,
        playback == BtPlayback::Playing, playback), false));
    s.end(token);
    assert(s.resumes == 0 && s.bt.connected && s.bt.playback == playback);
    assert(!s.route().radioOutput);
    assert(s.route().btOutput == (playback == BtPlayback::Playing));
    assert(s.route().outputRate == (playback == BtPlayback::Playing ? 44100U : 0U));
    assert(dacXsmtHigh(dacPlaybackForSource(
               true, false, playback == BtPlayback::Playing, playback), false) ==
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

  // Presentation regressions A-H use the production metadata gate plus the
  // real API and display view selectors. Temporary decoder text may be
  // observed by logs, but it cannot become canonical RADIO presentation.

  // A. RADIO STOP + HTTP 503: keep station, metadata and STOP.
  {
    Scenario error;
    error.radio(false);
    const auto errorToken = error.temporary.begin();
    assert(!error.decoderMetadata(errorToken, "",
                                  "HTTP/1.1 503 Service Unavailable"));
    error.end(errorToken);
    assert(error.cleanups == 1 && error.resumes == 0 && !error.radioRunning);
    assertRadioPresentation(error, false);
  }

  // B. RADIO STOP + timeout follows the same presentation-preserving cleanup.
  {
    Scenario timeout;
    timeout.radio(false);
    const auto timeoutToken = timeout.temporary.begin();
    timeout.backendStopped(timeoutToken);
    assert(timeout.cleanups == 1 && timeout.resumes == 0 && !timeout.radioRunning);
    assertRadioPresentation(timeout, false);
  }

  // C. RADIO PLAY + error reconnects once and exposes no temporary error.
  {
    Scenario error;
    error.radio(true);
    const auto errorToken = error.temporary.begin();
    assert(!error.decoderMetadata(errorToken, "TTS service",
                                  "HTTP/1.1 503 Service Unavailable"));
    error.end(errorToken);
    assert(error.cleanups == 1 && error.resumes == 1 && error.radioRunning);
    assertRadioPresentation(error, true);
  }

  // D. BT PLAY + error returns to the observed BT stream without commands.
  {
    Scenario error;
    error.radio(false);
    error.connect(BtPlayback::Playing);
    const auto errorToken = error.temporary.begin();
    assert(!error.decoderMetadata(errorToken, "TTS service", "HTTP 503"));
    error.end(errorToken);
    const WebStatusView web = error.webView();
    assert(std::strcmp(web.activeSource, "bt") == 0);
    assert(std::strcmp(web.name, "Telefon testowy") == 0);
    assert(std::strcmp(web.transport, "playing") == 0);
    assert(error.route().btOutput && error.resumes == 0 && error.cleanups == 1);
    assert(std::strcmp(error.stationName, "Radio test") == 0);
    assert(std::strcmp(error.metadata, "Stan bazowy") == 0);
  }

  // E. BT PAUSED/STOPPED + timeout never synthesizes RESUME or autoplay.
  for (const auto playback : {BtPlayback::Paused, BtPlayback::Stopped}) {
    Scenario timeout;
    timeout.radio(false);
    timeout.connect(playback);
    const auto timeoutToken = timeout.temporary.begin();
    timeout.backendStopped(timeoutToken);
    const WebStatusView web = timeout.webView();
    assert(std::strcmp(web.activeSource, "bt") == 0);
    assert(std::strcmp(web.transport,
        playback == BtPlayback::Paused ? "paused" : "stopped") == 0);
    assert(!timeout.route().btOutput && timeout.resumes == 0 &&
           timeout.cleanups == 1);
    assert(std::strcmp(timeout.stationName, "Radio test") == 0);
    assert(std::strcmp(timeout.metadata, "Stan bazowy") == 0);
  }

  // F. BT -> RADIO STOP during timeout uses the current source and keeps the
  // selected station. The earlier BT state is never restored late.
  {
    Scenario switched;
    switched.radio(false);
    switched.connect(BtPlayback::Playing);
    const auto timeoutToken = switched.temporary.begin();
    assert(switched.source.select(ActiveSource::Radio, switched.bt).activeChanged);
    switched.backendStopped(timeoutToken);
    assert(switched.source.active() == ActiveSource::Radio);
    assert(switched.resumes == 0 && !switched.radioRunning);
    assertRadioPresentation(switched, false);
  }

  // G. A stale generation cannot write after STOP cleanup or after RADIO PLAY
  // opened the gate for tokenless callbacks from the new radio connection.
  {
    Scenario stopped;
    stopped.radio(false);
    const auto staleToken = stopped.temporary.begin();
    stopped.end(staleToken);
    assert(!stopped.decoderMetadata(staleToken, "stale",
                                    "HTTP/1.1 503 stale"));
    assertRadioPresentation(stopped, false);

    Scenario playing;
    playing.radio(true);
    const auto oldToken = playing.temporary.begin();
    playing.end(oldToken);
    assert(playing.temporary.radioMetadataAllowed(0));
    assert(!playing.decoderMetadata(oldToken, "stale", "HTTP 503 stale"));
    assertRadioPresentation(playing, true);
  }

  // H. Duplicate terminal/finish notifications cannot repeat cleanup or
  // restore; the state contains no owned heap allocation to free twice.
  {
    Scenario duplicate;
    duplicate.radio(false);
    const auto duplicateToken = duplicate.temporary.begin();
    assert(duplicate.temporary.signal(duplicateToken));
    duplicate.radioRunning = false;
    assert(duplicate.temporary.finish(duplicateToken));
    ++duplicate.cleanups;
    duplicate.restore();
    assert(!duplicate.temporary.signal(duplicateToken));
    assert(!duplicate.temporary.finish(duplicateToken));
    duplicate.restore();
    assert(duplicate.cleanups == 1 && duplicate.resumes == 0 &&
           !duplicate.temporary.busy());
    assertRadioPresentation(duplicate, false);
  }

  // Progress-watchdog matrix A-J. These cases drive the same production
  // serviceTemporaryAudioTransport() dispatcher called by Player::loop(); the
  // test never calls Player::finishTemporary() or completes a timeout by hand.
  const uint32_t inactivity =
      voxone::temporary_audio::kInactivityTimeoutMs;

  // A/J. HTTP 503 remains a decoder terminal event. It wins before the
  // watchdog, cleans once and cannot publish temporary metadata to RADIO.
  {
    TransportScenario error;
    error.radio(false);
    const uint32_t token = error.beginTransport(100, 64);
    assert(!error.decoderMetadata(token, "TTS service",
                                  "HTTP/1.1 503 Service Unavailable"));
    assert(error.temporary.signal(token));
    assert(error.service(101, 64));
    assert(error.timeouts == 0 && error.cleanups == 1 && error.resumes == 0);
    assertRadioPresentation(error, false);
  }

  // B/J. A valid response with no body progress expires autonomously at the
  // contract boundary and preserves RADIO STOP presentation.
  {
    TransportScenario stalled;
    stalled.radio(false);
    const uint32_t token = stalled.beginTransport(1000, 96);
    assert(!stalled.service(1000 + inactivity - 1, 96));
    assert(stalled.service(1000 + inactivity, 96));
    assert(stalled.timeouts == 1 && stalled.cleanups == 1 &&
           stalled.resumes == 0 && !stalled.temporary.busy());
    assert(!stalled.temporary.signal(token));
    assertRadioPresentation(stalled, false);
  }

  // C. Partial body delivery starts a fresh inactivity window. Cleanup occurs
  // only after that new window elapses without another byte.
  {
    TransportScenario partial;
    partial.radio(false);
    partial.beginTransport(2000, 100);
    assert(!partial.service(2000 + inactivity - 1, 600));
    assert(!partial.service(2000 + 2 * inactivity - 2, 600));
    assert(partial.service(2000 + 2 * inactivity - 1, 600));
    assert(partial.timeouts == 1 && partial.cleanups == 1 &&
           partial.resumes == 0);
    assertRadioPresentation(partial, false);
  }

  // D. RADIO PLAY restores once after the same no-progress timeout.
  {
    TransportScenario playing;
    playing.radio(true, 23);
    playing.beginTransport(3000, 10);
    assert(playing.service(3000 + inactivity, 10));
    assert(playing.timeouts == 1 && playing.cleanups == 1 &&
           playing.resumes == 1 && playing.station == 23);
    assertRadioPresentation(playing, true);
  }

  // E. A timeout over BT PAUSED/STOPPED returns ownership without emitting a
  // RADIO restore or synthesizing a BT transport command.
  for (const auto playback : {BtPlayback::Paused, BtPlayback::Stopped}) {
    TransportScenario bt;
    bt.radio(false);
    bt.connect(playback);
    bt.beginTransport(4000, 20);
    assert(bt.service(4000 + inactivity, 20));
    const WebStatusView web = bt.webView();
    assert(bt.timeouts == 1 && bt.cleanups == 1 && bt.resumes == 0);
    assert(bt.source.active() == ActiveSource::Bluetooth);
    assert(std::strcmp(web.transport,
        playback == BtPlayback::Paused ? "paused" : "stopped") == 0);
    assert(!bt.route().btOutput);
  }

  // F/J. BT -> RADIO STOP during the stall makes the current source
  // authoritative. Timeout cannot restore the stale BT selection.
  {
    TransportScenario switched;
    switched.radio(false);
    switched.connect(BtPlayback::Playing);
    switched.beginTransport(5000, 30);
    assert(switched.source.select(ActiveSource::Radio, switched.bt)
               .activeChanged);
    assert(switched.service(5000 + inactivity, 30));
    assert(switched.source.active() == ActiveSource::Radio);
    assert(switched.resumes == 0 && switched.cleanups == 1);
    assertRadioPresentation(switched, false);
  }

  // G. A callback carrying the timed-out generation is stale after cleanup
  // and cannot signal, finish or restart the announcement.
  {
    TransportScenario late;
    late.radio(false);
    const uint32_t token = late.beginTransport(6000, 40);
    assert(late.service(6000 + inactivity, 40));
    assert(!late.temporary.signal(token));
    assert(!late.service(6000 + inactivity + 1, 41, true, true, false));
    assert(late.cleanups == 1 && late.timeouts == 1 && late.resumes == 0);
  }

  // H. Backend completion at the timeout boundary wins deterministically;
  // the common dispatcher requests cleanup once and records no second timeout.
  {
    TransportScenario race;
    race.radio(false);
    const uint32_t token = race.beginTransport(7000, 50);
    assert(race.service(7000 + inactivity, 50, false, false, false));
    assert(race.timeouts == 0 && race.cleanups == 1);
    assert(!race.temporary.signal(token));
    assert(!race.service(7000 + inactivity, 50, false, false, false));
    assert(race.cleanups == 1);
  }

  // F/L. Ten slow-body fragments at four-second intervals continually renew
  // the ten-second inactivity window. The last positive read may observe the
  // TCP close in the same Player cycle; a complete body remains decoder-owned.
  {
    TransportScenario slowBody;
    slowBody.radio(false);
    const uint32_t token = slowBody.beginTransport(8000, 0);
    uint32_t now = 8000;
    uint64_t bytes = 0;
    for (int chunk = 0; chunk < 10; ++chunk) {
      now += 4000;
      bytes += chunk == 0 ? 364844 : 364800;
      const bool complete = voxone::audio::webFileDataComplete(
          3648044, static_cast<uint32_t>(bytes), 44, 3648000);
      assert(complete == (chunk == 9));
      assert(!slowBody.service(now, bytes, true, !complete, false,
                               complete, complete ? 8192 : 0));
    }
    assert(bytes == 3648044);
    assert(!slowBody.service(now + 100, bytes, true, false, false, true,
                             4096));
    assert(!slowBody.service(now + 200, bytes, true, false, false, true, 64));
    assert(slowBody.temporary.signal(token));
    assert(slowBody.service(now + 201, bytes, true, false, false, true, 0));
    assert(slowBody.timeouts == 0 && slowBody.disconnects == 0 &&
           slowBody.cleanups == 1);
  }

  // A/C. Closing TCP immediately after a complete body does not impose a
  // one-loop grace period. The actual input-buffer level may need many decoder
  // cycles to drain before the synchronous EOF callback signals the run.
  {
    TransportScenario natural;
    natural.radio(false);
    const uint32_t token = natural.beginTransport(9000, 3648000);
    assert(!natural.service(9001, 3648044, true, false, false, true, 12288));
    assert(!natural.service(9640, 3648044, true, false, false, true, 12288));
    assert(!natural.service(9641, 3648044, true, false, false, true, 8192));
    assert(!natural.service(11000, 3648044, true, false, false, true, 4096));
    assert(!natural.service(12000, 3648044, true, false, false, true, 64));
    assert(natural.temporary.signal(token));
    assert(natural.service(12001, 3648044, true, false, false, true, 0));
    assert(natural.timeouts == 0 && natural.disconnects == 0 &&
           natural.cleanups == 1);
    assert(!natural.service(12002, 3648044, false, false, false, true, 0));
    assert(natural.cleanups == 1);
  }

  // B. A server which keeps TCP open after the declared body follows the same
  // complete-body -> decoder EOF -> one cleanup path.
  {
    TransportScenario keepAlive;
    keepAlive.radio(false);
    const uint32_t token = keepAlive.beginTransport(13000, 3648000);
    assert(!keepAlive.service(13001, 3648044, true, true, false, true, 4096));
    assert(!keepAlive.service(13002, 3648044, true, true, false, true, 128));
    assert(keepAlive.temporary.signal(token));
    assert(keepAlive.service(13003, 3648044, true, true, false, true, 0));
    assert(keepAlive.timeouts == 0 && keepAlive.disconnects == 0 &&
           keepAlive.cleanups == 1);
  }

  // H. A persistent close before Content-Length is complete is classified as
  // a transport disconnect on the next service cycle, not as a ten-second
  // watchdog expiry. The common cleanup path still runs exactly once.
  {
    TransportScenario truncated;
    truncated.radio(false);
    truncated.beginTransport(14000, 500);
    assert(!truncated.service(14639, 500, true, false, false));
    assert(truncated.service(14640, 500, true, false, false));
    assert(truncated.timeouts == 0 && truncated.disconnects == 1 &&
           truncated.cleanups == 1);
    assert(!truncated.service(14641, 500, true, false, false));
    assert(truncated.cleanups == 1);
  }

  // L. A final positive read and disconnect in the same cycle renew the
  // inactivity window. If the body is still incomplete, subsequent persistent
  // disconnect observations remain a transport error, never natural EOF.
  {
    TransportScenario lastPartialRead;
    lastPartialRead.radio(false);
    lastPartialRead.beginTransport(15000, 100);
    assert(!lastPartialRead.service(15001, 200, true, false, false, false, 64));
    assert(!lastPartialRead.service(15002, 200, true, false, false, false, 64));
    assert(lastPartialRead.service(15003, 200, true, false, false, false, 64));
    assert(lastPartialRead.timeouts == 0 &&
           lastPartialRead.disconnects == 1 &&
           lastPartialRead.cleanups == 1);
  }

  // A complete body which the decoder cannot drain is bounded by the same
  // ten-second inactivity contract; suppressing expected TCP close cannot
  // leave a temporary run active forever.
  {
    TransportScenario stuckDecoder;
    stuckDecoder.radio(false);
    stuckDecoder.beginTransport(16000, 3648000);
    assert(!stuckDecoder.service(16001, 3648044, true, false, false, true,
                                 4096));
    assert(!stuckDecoder.service(16001 + inactivity - 1, 3648044, true,
                                 false, false, true, 4096));
    assert(stuckDecoder.service(16001 + inactivity, 3648044, true, false,
                                false, true, 4096));
    assert(stuckDecoder.timeouts == 1 && stuckDecoder.disconnects == 0 &&
           stuckDecoder.cleanups == 1);
  }

  // J. STOP is already a terminal signal when the dispatcher runs. It wins
  // over both a simultaneous disconnect and the decoder-drain watchdog, and
  // the common cleanup remains one-shot.
  {
    TransportScenario stopped;
    stopped.radio(false);
    const uint32_t token = stopped.beginTransport(17000, 1000);
    assert(stopped.temporary.signal(token));
    assert(stopped.service(17001, 1000, true, false, false, false, 0));
    assert(stopped.timeouts == 0 && stopped.disconnects == 0 &&
           stopped.cleanups == 1);
    assert(!stopped.service(17002, 1000, false, false, false, false, 0));
    assert(stopped.cleanups == 1);
  }

  // I. A slow stream is unlimited while it keeps making real progress. A
  // complete buffered frame also prevents a false timeout under backpressure.
  {
    TransportScenario slow;
    slow.radio(false);
    const uint32_t token = slow.beginTransport(18000, 60);
    uint32_t now = 18000;
    uint64_t bytes = 60;
    for (int i = 0; i < 8; ++i) {
      now += inactivity - 1;
      ++bytes;
      assert(!slow.service(now, bytes));
    }
    assert(!slow.service(now + 2 * inactivity, bytes, true, true, true));
    assert(slow.temporary.signal(token));
    assert(slow.service(now + 2 * inactivity + 1, bytes));
    assert(slow.timeouts == 0 && slow.cleanups == 1 && slow.resumes == 0);
  }

  // Once a buffered frame drains after a long network gap, the already
  // elapsed inactivity is acted on immediately instead of granting a new gap.
  {
    TransportScenario drained;
    drained.radio(false);
    drained.beginTransport(19000, 70);
    assert(!drained.service(19000 + 2 * inactivity, 70, true, true, true));
    assert(drained.service(19000 + 2 * inactivity, 70, true, true, false));
    assert(drained.timeouts == 1 && drained.cleanups == 1);
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
  // Player treats STOP received during SOURCE_HA as ending only the temporary
  // stream, so it does not call radio(false) in that path.
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
