#include "../src/core/security_recovery_session.h"
#include "../src/hardware/hardware_descriptor.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

using namespace voxone::security_recovery;

namespace {

GateConditions eligible() {
  return {true, RecoveryImageState::Valid, false, false, true, false};
}

class FakeGateSource : public RecoveryGateSource {
 public:
  GateConditions conditions = eligible();
  unsigned reads = 0;

  GateConditions read() override {
    ++reads;
    return conditions;
  }
};

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void beginReady(RecoverySession& session, uint32_t now = 0) {
  const auto result = session.begin(KeyState::Accepted, now);
  assert(result.to == RecoveryState::Ready);
  assert(session.live());
  assert(!session.activated());
}

void beginAwaitingConfirmation(RecoverySession& session,
                               uint32_t now = 0) {
  beginReady(session, now);
  assert(session.activate(now + 1U).to == RecoveryState::Active);
  assert(session.activated());
  assert(session.awaitConfirmation(now + 2U).to ==
         RecoveryState::AwaitConfirmation);
}

}  // namespace

int main() {
  FakeGateSource gate;

  RecoverySession single(gate);
  beginReady(single, 100);
  assert(single.requestedThisBoot());
  const auto duplicate = single.begin(KeyState::Accepted, 101);
  assert(duplicate.diagnostic == RecoveryDiagnostic::DuplicateRequest);
  assert(single.state() == RecoveryState::Ready);

  RecoverySession ignored(gate);
  const unsigned readsBeforeIgnored = gate.reads;
  assert(ignored.begin(KeyState::Cancelled, 0).to == RecoveryState::Idle);
  assert(!ignored.live());
  assert(gate.reads == readsBeforeIgnored);
  assert(ignored.begin(KeyState::Accepted, 1).diagnostic ==
         RecoveryDiagnostic::DuplicateRequest);
  assert(ignored.state() == RecoveryState::Idle);

  RecoverySession cancelled(gate);
  beginReady(cancelled);
  assert(cancelled.activate(1).to == RecoveryState::Active);
  assert(cancelled.cancel(2).to == RecoveryState::Cancelled);
  assert(!cancelled.live() && !cancelled.activated());
  assert(cancelled.service(3).to == RecoveryState::Cancelled);

  RecoverySession completed(gate);
  beginAwaitingConfirmation(completed);
  assert(completed.confirm(3).to == RecoveryState::CommitReady);
  assert(completed.complete(true, 4).diagnostic ==
         RecoveryDiagnostic::InvalidEvent);
  assert(completed.live() && completed.activated());
  assert(completed.beginCommit(5).to == RecoveryState::Committing);
  assert(completed.complete(true, 6).to == RecoveryState::Completed);
  assert(!completed.live() && !completed.activated());

  RecoverySession sessionTimeout(gate);
  beginReady(sessionTimeout, 20);
  assert(sessionTimeout.service(20 + 599999U).to == RecoveryState::Ready);
  assert(sessionTimeout.service(20 + 600000U).diagnostic ==
         RecoveryDiagnostic::SessionTimeout);
  assert(!sessionTimeout.live());

  RecoverySession confirmationTimeout(gate);
  beginReady(confirmationTimeout);
  confirmationTimeout.activate(1);
  confirmationTimeout.awaitConfirmation(100);
  assert(confirmationTimeout.service(30099).to ==
         RecoveryState::AwaitConfirmation);
  assert(confirmationTimeout.service(30100).diagnostic ==
         RecoveryDiagnostic::ConfirmationTimeout);
  assert(!confirmationTimeout.live());

  GateConditions pending = eligible();
  pending.imageState = RecoveryImageState::PendingVerify;
  pending.pending = true;
  RecoverySession pendingActivation(gate);
  beginReady(pendingActivation);
  gate.conditions = pending;
  assert(pendingActivation.activate(1).diagnostic ==
         RecoveryDiagnostic::OtaPendingVerify);
  assert(pendingActivation.state() == RecoveryState::Failed);

  GateConditions upload = eligible();
  upload.otaUploadActive = true;
  gate.conditions = eligible();
  RecoverySession uploadActivation(gate);
  beginReady(uploadActivation);
  gate.conditions = upload;
  assert(uploadActivation.activate(1).diagnostic ==
         RecoveryDiagnostic::OtaUploadActive);

  GateConditions restore = eligible();
  restore.configRestoreActive = true;
  gate.conditions = eligible();
  RecoverySession restoreBeforeCommit(gate);
  beginAwaitingConfirmation(restoreBeforeCommit);
  gate.conditions = restore;
  assert(restoreBeforeCommit.confirm(3).diagnostic ==
         RecoveryDiagnostic::ConfigRestoreActive);
  assert(restoreBeforeCommit.state() == RecoveryState::Failed);

  GateConditions otaReadError = eligible();
  otaReadError.otaStatusReadable = false;
  gate.conditions = eligible();
  RecoverySession readError(gate);
  beginAwaitingConfirmation(readError);
  gate.conditions = otaReadError;
  assert(readError.confirm(3).diagnostic ==
         RecoveryDiagnostic::OtaStatusReadError);

  GateConditions configReadError = eligible();
  configReadError.configStatusReadable = false;
  gate.conditions = eligible();
  RecoverySession configError(gate);
  beginReady(configError);
  gate.conditions = configReadError;
  assert(configError.activate(1).diagnostic ==
         RecoveryDiagnostic::ConfigStatusReadError);

  gate.conditions = upload;
  RecoverySession initialGateFailure(gate);
  assert(initialGateFailure.begin(KeyState::Accepted, 0).diagnostic ==
         RecoveryDiagnostic::OtaUploadActive);
  assert(!initialGateFailure.live());

  gate.conditions = eligible();
  RecoverySession invalid(gate);
  const unsigned readsBeforeInvalid = gate.reads;
  assert(invalid.activate(0).diagnostic ==
         RecoveryDiagnostic::InvalidEvent);
  assert(invalid.confirm(0).diagnostic ==
         RecoveryDiagnostic::InvalidEvent);
  assert(invalid.complete(true, 0).diagnostic ==
         RecoveryDiagnostic::InvalidEvent);
  assert(invalid.state() == RecoveryState::Idle);
  assert(gate.reads == readsBeforeInvalid);

  RecoverySession confirmationRecheck(gate);
  beginAwaitingConfirmation(confirmationRecheck);
  const unsigned readsBeforeConfirmation = gate.reads;
  assert(confirmationRecheck.confirm(3).diagnostic ==
         RecoveryDiagnostic::ConfirmationAccepted);
  assert(gate.reads == readsBeforeConfirmation + 1U);

  RecoverySession commitRecheck(gate);
  beginAwaitingConfirmation(commitRecheck);
  assert(commitRecheck.confirm(3).to == RecoveryState::CommitReady);
  const unsigned readsBeforeCommit = gate.reads;
  assert(commitRecheck.beginCommit(4).to == RecoveryState::Committing);
  assert(gate.reads == readsBeforeCommit + 1U);

  gate.conditions = upload;
  RecoverySession commitDenied(gate);
  gate.conditions = eligible();
  beginAwaitingConfirmation(commitDenied);
  assert(commitDenied.confirm(3).to == RecoveryState::CommitReady);
  gate.conditions = upload;
  assert(commitDenied.beginCommit(4).diagnostic ==
         RecoveryDiagnostic::OtaUploadActive);
  assert(commitDenied.state() == RecoveryState::Failed);
  gate.conditions = eligible();

  const uint32_t wrapStart = UINT32_MAX - 100U;
  RecoverySession wrap(gate);
  beginReady(wrap, wrapStart);
  wrap.activate(wrapStart + 1U);
  wrap.awaitConfirmation(wrapStart + 2U);
  assert(wrap.service(wrapStart + 30001U).to ==
         RecoveryState::AwaitConfirmation);
  assert(wrap.service(wrapStart + 30002U).diagnostic ==
         RecoveryDiagnostic::ConfirmationTimeout);

  const auto& hardware = voxone::hardware::currentHardware();
#if defined(VOXONE_PROFILE_W0)
  assert(hardware.localInput.key == 32);
#elif defined(VOXONE_PROFILE_L0)
  assert(hardware.localInput.key == 39);
#else
  assert(hardware.localInput.key == 4);
#endif

  const std::string core =
      readFile("src/core/security_recovery_session.cpp");
  const std::string runtime =
      readFile("src/core/security_recovery_session_runtime.cpp");
  const std::string main = readFile("src/main.cpp");
  const std::string combined = core + runtime;
  assert(combined.find("new ") == std::string::npos);
  assert(combined.find("xTaskCreate") == std::string::npos);
  assert(combined.find("softAP") == std::string::npos);
  assert(combined.find("AsyncWebServer") == std::string::npos);
  assert(combined.find("Preferences") == std::string::npos);
  assert(combined.find("nvs_") == std::string::npos);
  assert(combined.find("esp_ota_mark_app_valid") == std::string::npos);
  assert(combined.find("esp_ota_mark_app_invalid") == std::string::npos);
  assert(combined.find("ESP.restart") == std::string::npos);
  assert(runtime.find("readRecoveryGateConditions()") != std::string::npos);
  assert(runtime.find("beginRecoveryCommit") != std::string::npos);
  assert(main.find("beginRecoverySession(") != std::string::npos);
  assert(main.find("serviceRecoverySession(millis())") != std::string::npos);
  assert(main.find("recoverAtBoot()") > main.find("beginRecoverySession("));
}
