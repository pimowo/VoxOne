#include "../src/core/update_display_view.h"
#include "../src/core/update_bar_render_state.h"
#include "../src/core/bt_update_progress.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

static std::string readSource(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main() {
  UpdateBarRenderState bar;
  auto delta = bar.apply({false, 0});
  assert(delta.reset && delta.fillX == 0 && delta.fillWidth == 56);
  delta = bar.step();
  assert(!delta.reset && delta.clearX == 0 && delta.clearWidth == 56 &&
         delta.fillX == 12 && delta.fillWidth == 56);
  delta = bar.apply({true, 25});
  assert(delta.reset && delta.fillX == 0 && delta.fillWidth == 89);
  delta = bar.apply({true, 26});
  assert(!delta.reset && delta.clearWidth == 0 &&
         delta.fillX == 89 && delta.fillWidth == 3);
  delta = bar.apply({true, 10});
  assert(!delta.reset && delta.clearX == 35 && delta.clearWidth == 57 &&
         delta.fillWidth == 0);
  delta = bar.apply({false, 0});
  assert(delta.reset && delta.fillX == 0 && delta.fillWidth == 56);
  bar.reset();  // New acquisition redraws the frame, even for the same value.
  delta = bar.apply({false, 0});
  assert(delta.reset && delta.fillWidth == 56);
  delta = bar.apply({true, 100});
  assert(delta.reset && delta.fillWidth == 356);

  UpdateBarRenderState oledBar(116, 18, 4);
  delta = oledBar.apply({false, 0});
  assert(delta.reset && delta.fillWidth == 18);
  delta = oledBar.step();
  assert(delta.clearWidth == 18 && delta.fillX == 4 && delta.fillWidth == 18);
  delta = oledBar.apply({true, 50});
  assert(delta.reset && delta.fillWidth == 58);
  delta = oledBar.apply({true, 100});
  assert(delta.fillX == 58 && delta.fillWidth == 58);

  assert(std::strcmp(updateTargetDisplayName(UpdateTarget::None), "") == 0);
  assert(std::strcmp(updateTargetDisplayName(UpdateTarget::VoxOneFirmware),
                     "VoxOne Firmware") == 0);
  assert(std::strcmp(updateTargetDisplayName(UpdateTarget::Filesystem),
                     "VoxOne system plików") == 0);
  assert(std::strcmp(updateTargetDisplayName(UpdateTarget::VoxOneBtFirmware),
                     "VoxOneBT Firmware") == 0);

  const struct { UpdateActivity activity; const char* text; } labels[] = {
      {UpdateActivity::None, ""},
      {UpdateActivity::PreparingUpdate, "Przygotowanie aktualizacji..."},
      {UpdateActivity::StoppingAudio, "Zatrzymywanie audio..."},
      {UpdateActivity::BackingUpSettings, "Tworzenie kopii ustawień..."},
      {UpdateActivity::WritingFirmware, "Zapisywanie firmware..."},
      {UpdateActivity::WritingFilesystem, "Zapisywanie systemu plików..."},
      {UpdateActivity::SendingToBt, "Wysyłanie do VoxOneBT..."},
      {UpdateActivity::Verifying, "Weryfikacja..."},
      {UpdateActivity::RestartingBt, "Restart VoxOneBT..."},
      {UpdateActivity::WaitingForBt, "Oczekiwanie na VoxOneBT..."},
      {UpdateActivity::HealthCheck, "Test nowego firmware..."},
      {UpdateActivity::Completed, "Aktualizacja zakończona"},
      {UpdateActivity::PreparingRestart, "Przygotowanie restartu..."},
      {UpdateActivity::Failed, "Błąd aktualizacji"},
  };
  for (const auto& label : labels)
    assert(std::strcmp(updateActivityDisplayText(label.activity), label.text) == 0);

  assert(std::strcmp(updateTargetCompactDisplayName(UpdateTarget::VoxOneFirmware),
                     "MAIN") == 0);
  assert(std::strcmp(updateTargetCompactDisplayName(UpdateTarget::Filesystem),
                     "SPIFFS") == 0);
  assert(std::strcmp(updateTargetCompactDisplayName(UpdateTarget::VoxOneBtFirmware),
                     "VoxOneBT") == 0);
  assert(std::strcmp(updateActivityCompactDisplayText(UpdateActivity::WritingFirmware),
                     "ZAPIS") == 0);
  assert(std::strcmp(updateActivityCompactDisplayText(UpdateActivity::Completed),
                     "GOTOWE") == 0);
  assert(std::strcmp(updateActivityCompactDisplayText(UpdateActivity::Failed),
                     "BLAD") == 0);

  UpdateProgressState state;
  UpdateDisplayProgressState view;
  auto snapshot = state.snapshot();
  assert(!updateScreenOwnsDisplay(snapshot));
  assert(updateScreenAllowsMode(snapshot, false));
  assert(state.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Preparing, 100));
  snapshot = state.snapshot();
  assert(snapshot.activity == UpdateActivity::PreparingUpdate);
  assert(updateScreenOwnsDisplay(snapshot));
  assert(!updateScreenAllowsMode(snapshot, false));  // VOL/timeout/metadata.
  assert(updateScreenAllowsMode(snapshot, true));
  assert(!view.observe(snapshot).determinate);
  state.phase(UpdatePhase::Writing);
  state.activity(UpdateActivity::WritingFirmware);
  state.progress(25, 100);
  snapshot = state.snapshot();
  assert(view.observe(snapshot).determinate && view.observe(snapshot).percent == 25);
  state.phase(UpdatePhase::Verifying);
  assert(!view.observe(state.snapshot()).determinate);
  state.terminal(UpdatePhase::Success);
  snapshot = state.snapshot();
  assert(snapshot.locked && !updateScreenReturnsToPlayer(snapshot));
  assert(view.observe(snapshot).determinate && view.observe(snapshot).percent == 100);
  state.restarting();
  assert(state.snapshot().activity == UpdateActivity::PreparingRestart);
  assert(view.observe(state.snapshot()).percent == 100);

  UpdateProgressState unknown;
  UpdateDisplayProgressState unknownView;
  assert(unknown.begin(UpdateTarget::Filesystem, UpdatePhase::Preparing));
  unknown.phase(UpdatePhase::Writing);
  unknown.activity(UpdateActivity::WritingFilesystem);
  unknown.progress(42, 0);
  assert(!unknownView.observe(unknown.snapshot()).determinate);
  unknown.terminal(UpdatePhase::Error);
  assert(updateScreenReturnsToPlayer(unknown.snapshot()));
  assert(!updateScreenOwnsDisplay(unknown.snapshot()));
  assert(!unknownView.observe(unknown.snapshot()).determinate);
  assert(unknown.begin(UpdateTarget::VoxOneBtFirmware, UpdatePhase::Preparing));
  assert(!unknownView.observe(unknown.snapshot()).determinate);
  unknown.terminal(UpdatePhase::Aborted);
  assert(updateScreenReturnsToPlayer(unknown.snapshot()));

  BtFirmwareSender::Progress bt;
  using State = BtFirmwareSender::State;
  bt.state = State::SendingData;
  assert(btUpdateActivity(bt, false) == UpdateActivity::SendingToBt);
  bt.state = State::WaitVerify;
  assert(btUpdateActivity(bt, false) == UpdateActivity::Verifying);
  bt.state = State::WaitIdentity;
  bt.phase = BtFirmwareSender::Phase::RestartingBt;
  assert(btUpdateActivity(bt, false) == UpdateActivity::RestartingBt);
  bt.phase = BtFirmwareSender::Phase::WaitingForBt;
  assert(btUpdateActivity(bt, false) == UpdateActivity::WaitingForBt);
  assert(btUpdateActivity(bt, true) == UpdateActivity::HealthCheck);
  bt.state = State::Success;
  assert(btUpdateActivity(bt, true) == UpdateActivity::Completed);

  // Existing DisplayTask owns the LCD while locked; queue/timeouts cannot
  // overwrite the page, and the no-display profile has its own no-op class.
  const auto display = readSource("src/core/display.cpp");
  assert(display.find("updateScreenOwnsDisplay(update)") != std::string::npos);
  assert(display.find("_swichMode(UPDATING);") != std::string::npos);
  assert(display.find("updateScreenAllowsMode(updateProgress(), newmode == UPDATING)") != std::string::npos);
  assert(display.find("updateScreenReturnsToPlayer(update)") != std::string::npos);
  assert(display.find("if (updateLockActive() && !(type == NEWMODE && payload == UPDATING)) return;") != std::string::npos);
  assert(display.find("class A0UpdateProgressWidget") != std::string::npos);
  assert(display.find("class C0UpdateProgressWidget") != std::string::npos);
  assert(display.find("progress_.determinate") != std::string::npos);
}
