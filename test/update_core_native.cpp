#include "../src/core/update_progress.h"
#include "../src/core/update_runtime_guard.h"
#include "../src/core/bt_update_progress.h"
#include "../src/core/update_restart_coordinator.h"
#include "../src/core/mqtt_update_shutdown_state.h"

#include <assert.h>
#include <fstream>
#include <iterator>
#include <string>
#include <string.h>

static std::string readSource(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

static void testRestartForTarget(UpdateTarget target) {
  UpdateProgressState update;
  UpdateRestartCoordinator coordinator;
  using Action = UpdateRestartCoordinator::Action;
  assert(coordinator.tick(update.snapshot(), 10) == Action::None);
  assert(update.begin(target, UpdatePhase::Preparing, 1024));
  assert(coordinator.tick(update.snapshot(), 20) == Action::None);
  update.phase(UpdatePhase::Verifying);
  assert(coordinator.tick(update.snapshot(), 30) == Action::None);
  update.terminal(UpdatePhase::Success);
  assert(update.snapshot().activity == UpdateActivity::Completed && update.locked());
  assert(!update.begin(UpdateTarget::Filesystem, UpdatePhase::Preparing));
  assert(coordinator.tick(update.snapshot(), 100) == Action::None);
  assert(coordinator.tick(update.snapshot(), 1299) == Action::None);
  assert(coordinator.tick(update.snapshot(), 1300) == Action::BeginNetworkQuiesce);
  update.restarting();
  assert(update.snapshot().activity == UpdateActivity::PreparingRestart);
  assert(update.locked());
  assert(coordinator.tick(update.snapshot(), 1301) == Action::None);  // Start once.
  assert(coordinator.tick(update.snapshot(), 1699) == Action::None);
  assert(coordinator.tick(update.snapshot(), 1700, true) == Action::None);
  assert(coordinator.tick(update.snapshot(), 1799, true) == Action::None);
  assert(coordinator.tick(update.snapshot(), 1800, true) == Action::Restart);
  assert(coordinator.tick(update.snapshot(), 2000, true) == Action::None);  // One shot.
  assert(update.locked());
  assert(!update.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Preparing));
}

int main() {
  MqttUpdateShutdownState disabledMqtt;
  assert(!disabledMqtt.complete(false));
  assert(disabledMqtt.begin());
  assert(disabledMqtt.complete(false));
  assert(!disabledMqtt.reconnectAllowed());

  MqttUpdateShutdownState disconnectedMqtt;
  assert(disconnectedMqtt.begin());
  assert(disconnectedMqtt.complete(false));

  MqttUpdateShutdownState connectedMqtt;
  unsigned disconnectRequests = 0;
  if (connectedMqtt.begin()) ++disconnectRequests;
  if (connectedMqtt.begin()) ++disconnectRequests;
  assert(disconnectRequests == 1);
  assert(!connectedMqtt.reconnectAllowed());  // Timer and Wi-Fi callbacks stay gated.
  assert(!connectedMqtt.complete(true));
  assert(connectedMqtt.complete(false));

  testRestartForTarget(UpdateTarget::VoxOneFirmware);
  testRestartForTarget(UpdateTarget::Filesystem);
  testRestartForTarget(UpdateTarget::VoxOneBtFirmware);

  using RestartAction = UpdateRestartCoordinator::Action;
  UpdateRestartCoordinator failedRestart;
  UpdateProgressState failedUpdate;
  assert(failedUpdate.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Writing));
  failedUpdate.terminal(UpdatePhase::Error);
  assert(failedRestart.tick(failedUpdate.snapshot(), 5000) == RestartAction::None);
  assert(!failedUpdate.locked());
  assert(failedUpdate.begin(UpdateTarget::Filesystem, UpdatePhase::Writing));
  failedUpdate.terminal(UpdatePhase::Aborted);
  assert(failedRestart.tick(failedUpdate.snapshot(), 7000) == RestartAction::None);
  assert(!failedUpdate.locked());

  UpdateProgressState btPending;
  UpdateRestartCoordinator btRestart;
  assert(btPending.begin(UpdateTarget::VoxOneBtFirmware, UpdatePhase::Sending));
  btPending.phase(UpdatePhase::Restarting);  // FW_OK, V0 has restarted.
  assert(btRestart.tick(btPending.snapshot(), 100) == RestartAction::None);
  btPending.phase(UpdatePhase::HealthCheck);  // PENDING_VERIFY, not VALID.
  assert(btRestart.tick(btPending.snapshot(), 100000) == RestartAction::None);
  btPending.terminal(UpdatePhase::Success);  // Only after OTA_STATE VALID.
  assert(btRestart.tick(btPending.snapshot(), 100001) == RestartAction::None);
  assert(btRestart.tick(btPending.snapshot(), 101201) == RestartAction::BeginNetworkQuiesce);

  UpdateProgressState stalled;
  UpdateRestartCoordinator stalledRestart;
  assert(stalled.begin(UpdateTarget::Filesystem, UpdatePhase::Writing));
  stalled.terminal(UpdatePhase::Success);
  assert(stalledRestart.tick(stalled.snapshot(), 100) == RestartAction::None);
  assert(stalledRestart.tick(stalled.snapshot(), 1300) == RestartAction::BeginNetworkQuiesce);
  stalled.restarting();
  assert(stalledRestart.tick(stalled.snapshot(), 2000) == RestartAction::None);
  assert(stalledRestart.tick(stalled.snapshot(), 4000) == RestartAction::None);
  assert(stalledRestart.tick(stalled.snapshot(), 6299) == RestartAction::None);
  assert(stalledRestart.tick(stalled.snapshot(), 6300) == RestartAction::RestartAfterTimeout);
  assert(stalledRestart.tick(stalled.snapshot(), 8000, true) == RestartAction::None);

  UpdateProgressState intermittent;
  UpdateRestartCoordinator intermittentRestart;
  assert(intermittent.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Writing));
  intermittent.terminal(UpdatePhase::Success);
  assert(intermittentRestart.tick(intermittent.snapshot(), 100) == RestartAction::None);
  assert(intermittentRestart.tick(intermittent.snapshot(), 1300) == RestartAction::BeginNetworkQuiesce);
  intermittent.restarting();
  assert(intermittentRestart.tick(intermittent.snapshot(), 1400, true) == RestartAction::None);
  assert(intermittentRestart.tick(intermittent.snapshot(), 1450, false) == RestartAction::None);
  assert(intermittentRestart.tick(intermittent.snapshot(), 1600, true) == RestartAction::None);
  assert(intermittentRestart.tick(intermittent.snapshot(), 1700, true) == RestartAction::Restart);

  UpdateProgressState mqttWaiting;
  UpdateRestartCoordinator mqttWaitingRestart;
  assert(mqttWaiting.begin(UpdateTarget::Filesystem, UpdatePhase::Writing));
  mqttWaiting.terminal(UpdatePhase::Success);
  assert(mqttWaitingRestart.tick(mqttWaiting.snapshot(), 100) == RestartAction::None);
  assert(mqttWaitingRestart.tick(mqttWaiting.snapshot(), 1300) == RestartAction::BeginNetworkQuiesce);
  mqttWaiting.restarting();
  const bool webReady = true;
  assert(mqttWaitingRestart.tick(mqttWaiting.snapshot(), 1800,
                                 webReady && connectedMqtt.complete(true)) == RestartAction::None);
  assert(mqttWaitingRestart.tick(mqttWaiting.snapshot(), 1801,
                                 webReady && connectedMqtt.complete(false)) == RestartAction::None);
  assert(mqttWaitingRestart.tick(mqttWaiting.snapshot(), 1901,
                                 webReady && connectedMqtt.complete(false)) == RestartAction::Restart);

  UpdateProgressState mqttStalled;
  UpdateRestartCoordinator mqttStalledRestart;
  assert(mqttStalled.begin(UpdateTarget::Filesystem, UpdatePhase::Writing));
  mqttStalled.terminal(UpdatePhase::Success);
  assert(mqttStalledRestart.tick(mqttStalled.snapshot(), 100) == RestartAction::None);
  assert(mqttStalledRestart.tick(mqttStalled.snapshot(), 1300) == RestartAction::BeginNetworkQuiesce);
  mqttStalled.restarting();
  assert(mqttStalledRestart.tick(mqttStalled.snapshot(), 6300,
                                 webReady && connectedMqtt.complete(true)) == RestartAction::RestartAfterTimeout);

  UpdateProgressState wrapping;
  UpdateRestartCoordinator wrapRestart;
  assert(wrapping.begin(UpdateTarget::Filesystem, UpdatePhase::Writing));
  wrapping.terminal(UpdatePhase::Success);
  const uint32_t nearWrap = 0xfffffff0u;
  assert(wrapRestart.tick(wrapping.snapshot(), nearWrap) == RestartAction::None);
  assert(wrapRestart.tick(wrapping.snapshot(), nearWrap + 1199u) == RestartAction::None);
  assert(wrapRestart.tick(wrapping.snapshot(), nearWrap + 1200u) == RestartAction::BeginNetworkQuiesce);
  wrapping.restarting();
  assert(wrapRestart.tick(wrapping.snapshot(), nearWrap + 1599u) == RestartAction::None);
  assert(wrapRestart.tick(wrapping.snapshot(), nearWrap + 1600u, true) == RestartAction::None);
  assert(wrapRestart.tick(wrapping.snapshot(), nearWrap + 1699u, true) == RestartAction::None);
  assert(wrapRestart.tick(wrapping.snapshot(), nearWrap + 1700u, true) == RestartAction::Restart);

  UpdateProgressState state;
  auto s = state.snapshot();
  assert(s.target == UpdateTarget::None && s.phase == UpdatePhase::Idle);
  assert(!s.active && !s.locked && !s.progressKnown);
  UpdateProgressState afterBoot;
  assert(afterBoot.snapshot().phase == UpdatePhase::Idle && !afterBoot.locked());

  assert(state.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Preparing, 100));
  s = state.snapshot();
  assert(s.target == UpdateTarget::VoxOneFirmware && s.active && s.locked);
  assert(!s.progressKnown);
  assert(!state.begin(UpdateTarget::Filesystem, UpdatePhase::Writing, 100));
  assert(state.snapshot().target == UpdateTarget::VoxOneFirmware);
  state.phase(UpdatePhase::Writing);
  state.progress(25, 100);
  s = state.snapshot();
  assert(s.progressKnown && s.doneBytes == 25 && s.totalBytes == 100 &&
         s.percent == 25);
  const uint32_t revision = s.revision;
  state.phase(UpdatePhase::Verifying);
  s = state.snapshot();
  assert(!s.progressKnown && s.percent == 0 && s.revision > revision);
  state.terminal(UpdatePhase::Success, 0, "finalized");
  s = state.snapshot();
  assert(s.phase == UpdatePhase::Success && !s.active && s.locked);
  assert(!state.begin(UpdateTarget::Filesystem, UpdatePhase::Writing, 100));
  state.terminal(UpdatePhase::Error, 7);  // Late callback cannot release a success lock.
  assert(state.snapshot().phase == UpdatePhase::Success && state.locked());
  state.restarting();
  assert(state.snapshot().phase == UpdatePhase::Restarting && state.locked());

  UpdateProgressState filesystem;
  assert(filesystem.begin(UpdateTarget::Filesystem, UpdatePhase::Writing, 196608));
  filesystem.progress(196608, 196608);
  filesystem.phase(UpdatePhase::Verifying);
  filesystem.terminal(UpdatePhase::Success);
  assert(filesystem.snapshot().target == UpdateTarget::Filesystem &&
         filesystem.snapshot().phase == UpdatePhase::Success && filesystem.locked());
  assert(!filesystem.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Preparing));

  UpdateProgressState next;
  assert(next.begin(UpdateTarget::Filesystem, UpdatePhase::Preparing));
  next.phase(UpdatePhase::Writing);
  next.progress(100, 0);
  s = next.snapshot();
  assert(!s.progressKnown && s.percent == 0);
  next.terminal(UpdatePhase::Error, 7, "flash failed");
  s = next.snapshot();
  assert(!s.locked && !s.active && s.phase == UpdatePhase::Error &&
         s.errorCode == 7 && strcmp(s.status, "flash failed") == 0);
  assert(next.begin(UpdateTarget::VoxOneBtFirmware, UpdatePhase::Preparing, 1024));
  s = next.snapshot();
  assert(s.errorCode == 0 && s.status[0] == '\0' && s.doneBytes == 0 &&
         s.totalBytes == 1024 && !s.progressKnown);
  next.phase(UpdatePhase::Sending);
  next.progress(512, 1024);
  assert(next.snapshot().percent == 50);
  next.terminal(UpdatePhase::Aborted);
  assert(!next.snapshot().locked && next.snapshot().phase == UpdatePhase::Aborted);
  assert(next.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Writing, 10));
  assert(next.snapshot().target == UpdateTarget::VoxOneFirmware &&
         next.snapshot().doneBytes == 0 && next.snapshot().percent == 0);

  using State = BtFirmwareSender::State;
  BtFirmwareSender::Progress bt;
  bt.state = State::SendingData;
  assert(btUpdatePhase(bt) == UpdatePhase::Sending);
  bt.state = State::WaitAck;
  assert(btUpdatePhase(bt) == UpdatePhase::Sending);
  bt.state = State::WaitVerify;
  assert(btUpdatePhase(bt) == UpdatePhase::Verifying);
  bt.state = State::WaitOk;
  assert(btUpdatePhase(bt) == UpdatePhase::Verifying);
  bt.state = State::WaitIdentity;
  bt.phase = BtFirmwareSender::Phase::RestartingBt;
  assert(btUpdatePhase(bt) == UpdatePhase::Restarting);  // FW_OK is not success.
  bt.phase = BtFirmwareSender::Phase::WaitingForBt;
  assert(btUpdatePhase(bt) == UpdatePhase::HealthCheck);  // PENDING_VERIFY.
  bt.state = State::Success;
  assert(btUpdatePhase(bt) == UpdatePhase::Success);      // Only after VALID.
  UpdateProgressState btUpdate;
  assert(btUpdate.begin(UpdateTarget::VoxOneBtFirmware, UpdatePhase::Preparing, 1024));
  btUpdate.phase(btUpdatePhase(bt));
  btUpdate.terminal(btUpdatePhase(bt));
  assert(btUpdate.snapshot().target == UpdateTarget::VoxOneBtFirmware &&
         btUpdate.snapshot().phase == UpdatePhase::Success && btUpdate.locked());
  assert(!btUpdate.begin(UpdateTarget::Filesystem, UpdatePhase::Preparing));
  bt.state = State::Error;
  assert(btUpdatePhase(bt) == UpdatePhase::Error);
  UpdateProgressState btError;
  assert(btError.begin(UpdateTarget::VoxOneBtFirmware, UpdatePhase::Sending, 1024));
  btError.terminal(btUpdatePhase(bt), 11);
  assert(btError.snapshot().phase == UpdatePhase::Error && !btError.locked());
  assert(btError.begin(UpdateTarget::Filesystem, UpdatePhase::Preparing));
  bt.state = State::Aborted;
  assert(btUpdatePhase(bt) == UpdatePhase::Aborted);
  UpdateProgressState btAborted;
  assert(btAborted.begin(UpdateTarget::VoxOneBtFirmware, UpdatePhase::Sending, 1024));
  btAborted.terminal(btUpdatePhase(bt));
  assert(btAborted.snapshot().phase == UpdatePhase::Aborted && !btAborted.locked());
  assert(btAborted.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Preparing));

  // Each lock acquisition quiesces once, regardless of progress callbacks.
  UpdateRuntimeGuard guard;
  UpdateProgressState runtime;
  assert(!guard.needsQuiesce(runtime.snapshot()));
  assert(runtime.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Preparing));
  const uint32_t first = runtime.snapshot().acquisition;
  unsigned quiesceCount = 0;
  if (guard.needsQuiesce(runtime.snapshot())) ++quiesceCount;
  runtime.phase(UpdatePhase::Writing);
  runtime.progress(1, 10);
  if (guard.needsQuiesce(runtime.snapshot())) ++quiesceCount;
  assert(quiesceCount == 1);
  runtime.terminal(UpdatePhase::Success);
  assert(!guard.needsQuiesce(runtime.snapshot()));
  assert(!runtime.begin(UpdateTarget::Filesystem, UpdatePhase::Preparing));
  UpdateProgressState afterRestart;
  UpdateRuntimeGuard freshGuard;
  assert(!freshGuard.needsQuiesce(afterRestart.snapshot()));

  UpdateProgressState failures;
  assert(failures.begin(UpdateTarget::VoxOneBtFirmware, UpdatePhase::Preparing));
  assert(freshGuard.needsQuiesce(failures.snapshot()));
  failures.terminal(UpdatePhase::Error);
  assert(!freshGuard.needsQuiesce(failures.snapshot()));
  assert(failures.begin(UpdateTarget::Filesystem, UpdatePhase::Preparing));
  assert(failures.snapshot().acquisition == first + 1);
  assert(freshGuard.needsQuiesce(failures.snapshot()));
  failures.terminal(UpdatePhase::Aborted);
  assert(failures.begin(UpdateTarget::VoxOneFirmware, UpdatePhase::Preparing));
  assert(freshGuard.needsQuiesce(failures.snapshot()));

  // Firmware wiring: quiesce is shared by Web Update and BT OTA, while the
  // UART/status loop remains live and only normal Player commands are gated.
  const auto server = readSource("src/core/netserver.cpp");
  const auto player = readSource("src/core/player.cpp");
  const auto source = readSource("src/core/source_manager.cpp");
  const auto mainLoop = readSource("src/main.cpp");
  const auto controls = readSource("src/core/controls.cpp");
  assert(server.find("UpdateRuntimeGuard updateRuntimeGuard;") != std::string::npos);
  assert(server.find("if (!updateRuntimeGuard.needsQuiesce(snapshot)) return true;") != std::string::npos);
  assert(server.find("player.sendCommand({PR_UPDATE_STOP, 0});") != std::string::npos);
  assert(server.find("if (!quiesceForUpdate()) btLink.abortFirmwareUpdate();") != std::string::npos);
  assert(server.find("session->audioBlocked = true;\n    if (!quiesceForUpdate())") != std::string::npos);
  assert(player.find("request.type != PR_UPDATE_STOP") != std::string::npos);
  assert(player.find("requestP.type != PR_UPDATE_STOP") != std::string::npos);
  assert(player.find("case PR_UPDATE_STOP:") != std::string::npos);
  assert(source.find("if (systemUpdateAudioBlocked()) return;") != std::string::npos);
  assert(controls.find("if(updateLockActive() || display.mode()==UPDATING") != std::string::npos);
  assert(mainLoop.find("btLink.loop();") != std::string::npos);
  assert(mainLoop.find("netserver.serviceBtFirmwareUpdate();") != std::string::npos);
  assert(mainLoop.find("netserver.serviceUpdateRestart();") != std::string::npos);
  assert(server.find("scheduleSystemRestart(\"UPDATE\")") == std::string::npos);
  assert(server.find("updateRestartCoordinator.tick(snapshot, millis(), networkReady)") != std::string::npos);
  assert(server.find("webserver.end();") != std::string::npos);
  assert(server.find("websocket.closeAll(1001") != std::string::npos);
  assert(server.find("webserver.activeRequestCount()") != std::string::npos);
  assert(server.find("mqttBeginUpdateShutdown();") != std::string::npos);
  assert(server.find("mqttReady && webReady") != std::string::npos);
  const std::string mqttSource = readSource("src/core/mqtt.cpp");
  assert(mqttSource.find("mqttClient.disconnect(true);") != std::string::npos);
  assert(mqttSource.find("updateShutdown.reconnectAllowed()") != std::string::npos);
}
