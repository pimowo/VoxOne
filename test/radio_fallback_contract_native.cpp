#include "../src/core/bt_link_protocol.h"
#include "../src/core/radio_source_policy.h"
#include "../src/core/source_manager_state.h"
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

  void connected(uint32_t nowMs) {
    line("STATUS_BEGIN", nowMs++);
    line("PROTO 2", nowMs++);
    line("FW_VERSION 0.6.7-dev", nowMs++);
    line("CAPS A2DP AVRCP", nowMs++);
    line("OTA_STATE NOT_PENDING", nowMs++);
    line("CONNECTED", nowMs++);
    line("DEVICE Contract phone", nowMs++);
    line("SAMPLE_RATE 44100", nowMs++);
    line("PLAYING", nowMs++);
    line("STATUS_END", nowMs);
  }
};

enum class RadioWork { Play, Stop, Suspend, Resume };

struct RadioCommand {
  RadioWork work;
  uint16_t station;
};

struct Scenario {
  SourceManagerState source;
  TemporaryAudioState temporary;
  std::vector<RadioCommand> queue;
  std::vector<RadioCommand> executed;
  bool radioPlaying = false;
  uint16_t lastStation = 3;

  void userPlay(uint16_t station) {
    source.recordRadioCommand(true, station);
    queue.push_back({RadioWork::Play, station});
  }

  void userStop() {
    source.recordRadioCommand(false);
    queue.push_back({RadioWork::Stop, 0});
  }

  void applySourceUpdate(const SourceUpdate& update, bool temporaryBusy) {
    if (!update.activeChanged || temporaryBusy) return;
    const RadioSourceActions actions = source.radioActions(update, radioPlaying);
    const uint16_t resumeStation = source.radioStationForResume(lastStation);
    queue.clear();  // Player::resetQueue() in stopOnSourceChange().
    if (actions.userStop) queue.push_back({RadioWork::Stop, 0});
    if (actions.suspend) queue.push_back({RadioWork::Suspend, 0});
    if (actions.resume)
      queue.push_back({RadioWork::Resume, resumeStation});
  }

  void drain() {
    for (const RadioCommand& command : queue) {
      switch (command.work) {
        case RadioWork::Play:
          lastStation = command.station;
          source.radioPlayConsumed();
          radioPlaying = true;
          executed.push_back(command);
          break;
        case RadioWork::Stop:
          source.radioStopConsumed();
          radioPlaying = false;
          executed.push_back(command);
          break;
        case RadioWork::Suspend:
          radioPlaying = false;
          executed.push_back(command);
          break;
        case RadioWork::Resume:
          // Player rechecks current intent when a queued resume is consumed.
          if (source.radioResumeAllowed()) {
            lastStation = command.station;
            source.radioPlayConsumed();
            radioPlaying = true;
            executed.push_back(command);
          }
          break;
      }
    }
    queue.clear();
  }

  bool finishTemporary(uint32_t token, bool networkReady = true,
                       bool blocked = false) {
    assert(temporary.signal(token));
    assert(temporary.finish(token));
    uint16_t station = lastStation;
    const bool restore = temporary.takeRadioRestore(
        source.radioResumeAllowed(), networkReady, blocked);
    if (restore) {
      station = source.radioStationForResume(station);
      source.radioPlayConsumed();
      lastStation = station;
      radioPlaying = true;
      executed.push_back({RadioWork::Resume, station});
    }
    return restore;
  }
};

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void auditProductionWiring() {
  // Behavioral checks below exercise the production state objects. This
  // narrow wiring check additionally binds them to the real Player commands.
  const std::string source = readFile("src/core/source_manager.cpp");
  const size_t begin = source.find("void stopOnSourceChange(");
  const size_t end = source.find("void applyManualSourceChange(", begin);
  assert(begin != std::string::npos && end != std::string::npos);
  const std::string body = source.substr(begin, end - begin);
  const size_t decision = body.find("sourceState.radioActions(update, radioActive)");
  const size_t reset = body.find("player.resetQueue()");
  const size_t stop = body.find("player.sendCommand({PR_STOP, 0})");
  const size_t suspend = body.find("player.sendCommand({PR_RADIO_SUSPEND, 0})");
  const size_t resume = body.find("player.sendCommand({PR_RADIO_RESUME, resumeStation})");
  assert(decision < reset && reset < stop && stop < suspend && suspend < resume);

  const std::string player = readFile("src/core/player.cpp");
  assert(player.find("case PR_RADIO_RESUME:") != std::string::npos);
  assert(player.find("if (sourceManagerRadioResumeAllowed())") !=
         std::string::npos);
  assert(player.find("sourceManagerTakeTemporaryRestore(_temporary") !=
         std::string::npos);
}

}  // namespace

int main() {
  auditProductionWiring();

  // RADIO PLAY -> automatic BT selection -> disconnect restores exactly the
  // saved station. Repeated identical observations produce no extra command.
  {
    LinkHarness link;
    Scenario scenario;
    link.link.begin(0);
    scenario.userPlay(17);
    scenario.drain();
    assert(scenario.radioPlaying && scenario.lastStation == 17);

    link.connected(1);
    SourceUpdate update = scenario.source.observe(link.link.state());
    assert(update.reason == SourceChangeReason::BtConnect);
    scenario.applySourceUpdate(update, false);
    assert(scenario.queue.size() == 1 &&
           scenario.queue[0].work == RadioWork::Suspend);
    scenario.drain();
    assert(!scenario.radioPlaying && scenario.source.radioPlayIntent());
    assert(!radioStopUpdatesSmartStart(RadioStopReason::SourceSwitch, false));
    const size_t afterSuspend = scenario.executed.size();
    for (int repeat = 0; repeat < 64; ++repeat) {
      update = scenario.source.observe(link.link.state());
      scenario.applySourceUpdate(update, false);
      scenario.drain();
    }
    assert(scenario.executed.size() == afterSuspend);

    link.line("DISCONNECTED", 20);
    update = scenario.source.observe(link.link.state());
    assert(update.reason == SourceChangeReason::BtDisconnect);
    scenario.applySourceUpdate(update, false);
    assert(scenario.queue.size() == 1 &&
           scenario.queue[0].work == RadioWork::Resume &&
           scenario.queue[0].station == 17);
    scenario.drain();
    assert(scenario.radioPlaying && scenario.lastStation == 17);
    const size_t afterResume = scenario.executed.size();
    for (int repeat = 0; repeat < 64; ++repeat) {
      update = scenario.source.observe(link.link.state());
      scenario.applySourceUpdate(update, false);
      scenario.drain();
    }
    assert(scenario.executed.size() == afterResume);
  }

  // RADIO STOP remains STOP. A passive fallback does not synthesize even a
  // redundant STOP, while a queued explicit STOP survives queue replacement
  // and is consumed exactly once.
  {
    LinkHarness link;
    Scenario passiveStop;
    link.link.begin(0);
    link.connected(1);
    SourceUpdate update = passiveStop.source.observe(link.link.state());
    passiveStop.applySourceUpdate(update, false);
    passiveStop.drain();
    link.line("DISCONNECTED", 20);
    update = passiveStop.source.observe(link.link.state());
    passiveStop.applySourceUpdate(update, false);
    assert(passiveStop.queue.empty());
    assert(!passiveStop.radioPlaying && !passiveStop.source.radioPlayIntent());

    LinkHarness explicitLink;
    Scenario explicitStop;
    explicitLink.link.begin(0);
    explicitLink.connected(1);
    update = explicitStop.source.observe(explicitLink.link.state());
    explicitStop.applySourceUpdate(update, false);
    explicitStop.drain();
    explicitStop.userStop();
    explicitLink.line("DISCONNECTED", 20);
    update = explicitStop.source.observe(explicitLink.link.state());
    explicitStop.applySourceUpdate(update, false);
    assert(explicitStop.queue.size() == 1 &&
           explicitStop.queue[0].work == RadioWork::Stop);
    explicitStop.drain();
    assert(!explicitStop.radioPlaying &&
           !explicitStop.source.radioPlayIntent());
    assert(explicitStop.executed.size() == 1 &&
           explicitStop.executed[0].work == RadioWork::Stop);
    update = explicitStop.source.observe(explicitLink.link.state());
    explicitStop.applySourceUpdate(update, false);
    explicitStop.drain();
    assert(explicitStop.executed.size() == 1);
  }

  // Manual RADIO wins while the existing phone remains connected. Only a new
  // connection edge may select BT again.
  {
    LinkHarness link;
    Scenario scenario;
    link.link.begin(0);
    scenario.userPlay(23);
    scenario.drain();
    link.connected(1);
    SourceUpdate update = scenario.source.observe(link.link.state());
    scenario.applySourceUpdate(update, false);
    scenario.drain();
    update = scenario.source.select(ActiveSource::Radio, link.link.state());
    assert(update.reason == SourceChangeReason::Manual);
    scenario.applySourceUpdate(update, false);
    scenario.drain();
    assert(scenario.radioPlaying && scenario.source.active() == ActiveSource::Radio);
    const size_t afterManualRadio = scenario.executed.size();
    for (int repeat = 0; repeat < 64; ++repeat) {
      update = scenario.source.observe(link.link.state());
      scenario.applySourceUpdate(update, false);
      scenario.drain();
    }
    assert(scenario.source.active() == ActiveSource::Radio);
    assert(scenario.executed.size() == afterManualRadio);
    link.line("DISCONNECTED", 30);
    update = scenario.source.observe(link.link.state());
    scenario.applySourceUpdate(update, false);
    scenario.drain();
    assert(scenario.source.active() == ActiveSource::Radio);
    link.line("CONNECTED", 31);
    link.line("SAMPLE_RATE 44100", 32);
    link.line("PLAYING", 33);
    update = scenario.source.observe(link.link.state());
    assert(update.reason == SourceChangeReason::BtConnect);
    scenario.applySourceUpdate(update, false);
    scenario.drain();
    assert(scenario.source.active() == ActiveSource::Bluetooth &&
           !scenario.radioPlaying);
  }

  // BT loss during TTS changes the base to RADIO, but performs no immediate
  // RADIO command and consumes restoration once without autoplay. The PLAY
  // intent remains available for a later, ordinary TTS cycle.
  {
    LinkHarness link;
    Scenario scenario;
    link.link.begin(0);
    scenario.userPlay(29);
    scenario.drain();
    link.connected(1);
    SourceUpdate update = scenario.source.observe(link.link.state());
    scenario.applySourceUpdate(update, false);
    scenario.drain();
    assert(!scenario.radioPlaying && scenario.source.radioPlayIntent());

    const uint32_t token = scenario.temporary.begin();
    const size_t beforeLoss = scenario.executed.size();
    link.line("DISCONNECTED", 20);
    update = scenario.source.observe(link.link.state());
    assert(temporaryBtLossRequiresRadioStop(update, scenario.temporary.busy()));
    assert(scenario.temporary.suppressRadioRestore());
    scenario.applySourceUpdate(update, scenario.temporary.busy());
    scenario.drain();
    assert(scenario.executed.size() == beforeLoss);
    assert(!scenario.finishTemporary(token));
    assert(!scenario.radioPlaying && scenario.source.radioPlayIntent());
    assert(!scenario.temporary.takeRadioRestore(true, true, false));

    const uint32_t later = scenario.temporary.begin();
    assert(scenario.finishTemporary(later));
    assert(scenario.radioPlaying && scenario.lastStation == 29);
    assert(!scenario.temporary.takeRadioRestore(true, true, false));
  }

  // A user STOP accepted after RESUME was queued invalidates that stale
  // resume before Player consumes it, preventing a transient autoplay.
  {
    LinkHarness link;
    Scenario scenario;
    link.link.begin(0);
    scenario.userPlay(31);
    scenario.drain();
    link.connected(1);
    SourceUpdate update = scenario.source.observe(link.link.state());
    scenario.applySourceUpdate(update, false);
    scenario.drain();
    link.line("DISCONNECTED", 20);
    update = scenario.source.observe(link.link.state());
    scenario.applySourceUpdate(update, false);
    assert(scenario.queue.size() == 1 &&
           scenario.queue[0].work == RadioWork::Resume);
    scenario.userStop();
    scenario.drain();
    assert(!scenario.radioPlaying && !scenario.source.radioPlayIntent());
    assert(scenario.executed.back().work == RadioWork::Stop);
    for (const RadioCommand& command : scenario.executed)
      assert(!(command.work == RadioWork::Resume && command.station == 31));
  }
}
