#include "../src/core/radio_source_policy.h"
#include "../src/core/source_manager_state.h"

#include <cassert>
#include <vector>

enum class RadioWork { Play, Stop, Suspend, Resume };
struct RadioRequest {
  RadioWork work;
  uint16_t station;
};

static void replaceQueuedWork(std::vector<RadioRequest>& queue,
                              const RadioSourceActions& actions,
                              uint16_t lastStation) {
  queue.clear();
  if (actions.userStop) queue.push_back({RadioWork::Stop, 0});
  if (actions.suspend) queue.push_back({RadioWork::Suspend, 0});
  if (actions.resume) queue.push_back({RadioWork::Resume, lastStation});
}

int main() {
  BtLinkState bt{};
  bt.runtimeAvailable = true;
  bt.connected = true;
  bt.playback = BtPlayback::Paused;

  SourceManagerState radio;
  assert(!radio.radioPlayIntent());
  assert(!radio.radioResumeAllowed());

  // A queued user PLAY is remembered even if switching discards that queue.
  const uint16_t lastStation = 17;
  std::vector<RadioRequest> queue{{RadioWork::Play, lastStation}};
  radio.recordRadioCommand(true, lastStation);
  SourceUpdate update = radio.cycle(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::Manual);
  RadioSourceActions actions = radio.radioActions(update, false);
  replaceQueuedWork(queue, actions, lastStation);
  assert(queue.empty());
  assert(radio.radioPlayIntent());
  assert(radio.radioStationForResume(3) == lastStation);

  update = radio.cycle(bt);
  actions = radio.radioActions(update, false);
  replaceQueuedWork(queue, actions, lastStation);
  assert(actions.resume && !actions.suspend);
  assert(queue.size() == 1 && queue[0].work == RadioWork::Resume);
  assert(queue[0].station == lastStation);
  assert(radio.radioResumeAllowed());
  radio.radioPlayConsumed();
  assert(radio.radioStationForResume(3) == 3);

  // RADIO PLAY: physical suspension preserves PLAY and smartstart.
  queue.clear();
  update = radio.cycle(bt);
  actions = radio.radioActions(update, true);
  replaceQueuedWork(queue, actions, lastStation);
  assert(actions.suspend && !actions.resume);
  assert(queue.size() == 1 && queue[0].work == RadioWork::Suspend);
  assert(radio.radioPlayIntent());
  assert(!radioStopUpdatesSmartStart(RadioStopReason::SourceSwitch, false));
  assert(!radioPlayPreparationUpdatesSmartStart(true, 1));

  update = radio.cycle(bt);
  actions = radio.radioActions(update, false);
  replaceQueuedWork(queue, actions, lastStation);
  assert(queue.size() == 1 && queue[0].work == RadioWork::Resume);
  assert(radio.radioPlayIntent());

  // USER STOP changes intent and retains the ordinary smartstart rule.
  radio.recordRadioCommand(false);
  assert(!radio.radioPlayIntent());
  assert(radio.radioStationForResume(3) == 3);
  assert(radioStopUpdatesSmartStart(RadioStopReason::Normal, false));
  assert(!radioStopUpdatesSmartStart(RadioStopReason::Normal, true));
  assert(radioPlayPreparationUpdatesSmartStart(false, 1));
  assert(!radioPlayPreparationUpdatesSmartStart(false, 2));

  // A queued explicit STOP survives the source switch and keeps persistence.
  queue.push_back({RadioWork::Stop, 0});
  update = radio.cycle(bt);
  actions = radio.radioActions(update, true);
  replaceQueuedWork(queue, actions, lastStation);
  assert(actions.userStop && !actions.suspend && !actions.resume);
  assert(queue.size() == 1 && queue[0].work == RadioWork::Stop);
  radio.radioStopConsumed();
  update = radio.cycle(bt);
  replaceQueuedWork(queue, radio.radioActions(update, false), lastStation);
  assert(queue.empty() && !radio.radioResumeAllowed());

  // A STOP arriving after a resume was queued prevents stale autoplay.
  radio.recordRadioCommand(true);
  update = radio.cycle(bt);
  update = radio.cycle(bt);
  replaceQueuedWork(queue, radio.radioActions(update, false), lastStation);
  assert(queue.size() == 1 && queue[0].work == RadioWork::Resume);
  radio.recordRadioCommand(false);
  assert(!radio.radioResumeAllowed());

  // Automatic BT disconnect remains a STOP fallback in SOURCE-1A.
  SourceManagerState automatic;
  automatic.recordRadioCommand(true);
  update = automatic.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtConnect);
  assert(!automatic.radioActions(update, true).resume);
  bt.connected = false;
  update = automatic.observe(bt);
  assert(update.activeChanged && update.reason == SourceChangeReason::BtDisconnect);
  assert(!automatic.radioActions(update, false).resume);
  assert(automatic.radioPlayIntent());

  // Manual RADIO remains selected while BT stays connected.
  bt.connected = true;
  update = radio.observe(bt);
  assert(update.activeChanged && radio.active() == ActiveSource::Bluetooth);
  update = radio.cycle(bt);
  assert(update.activeChanged && radio.active() == ActiveSource::Radio);
  for (int i = 0; i < 31; ++i)
    assert(!radio.observe(bt).activeChanged &&
           radio.active() == ActiveSource::Radio);
}
