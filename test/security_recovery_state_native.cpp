#include "../src/core/security_recovery_state.h"
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

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void reachOtaCheck(RecoveryStateMachine& machine, uint32_t now = 0) {
  assert(machine.observeBootKey(KeyState::Accepted, now).to ==
         RecoveryState::Requested);
  assert(machine.dispatch(RecoveryEvent::BeginOtaCheck, now).to ==
         RecoveryState::OtaCheck);
}

void reachReady(RecoveryStateMachine& machine, uint32_t now = 0) {
  reachOtaCheck(machine, now);
  assert(machine.evaluateGate(eligible(), now).to == RecoveryState::Ready);
}
}  // namespace

int main() {
  RecoveryStateMachine accepted;
  assert(accepted.observeBootKey(KeyState::Accepted, 100).to ==
         RecoveryState::Requested);
  assert(accepted.requestedThisBoot());
  assert(accepted.observeBootKey(KeyState::Accepted, 101).diagnostic ==
         RecoveryDiagnostic::DuplicateRequest);

  RecoveryStateMachine cancelledKey;
  assert(cancelledKey.observeBootKey(KeyState::Cancelled, 0).to ==
         RecoveryState::Idle);
  assert(!cancelledKey.requestedThisBoot());
  assert(cancelledKey.observeBootKey(KeyState::Accepted, 1).diagnostic ==
         RecoveryDiagnostic::DuplicateRequest);
  assert(cancelledKey.state() == RecoveryState::Idle);

  RecoveryStateMachine stuckKey;
  assert(stuckKey.observeBootKey(KeyState::StuckLow, 0).to ==
         RecoveryState::Idle);
  assert(!stuckKey.requestedThisBoot());

  RecoveryStateMachine valid;
  reachOtaCheck(valid);
  assert(valid.evaluateGate(eligible(), 0).diagnostic ==
         RecoveryDiagnostic::Ready);

  struct DenialCase {
    GateConditions conditions;
    RecoveryDiagnostic diagnostic;
  };
  const DenialCase denials[] = {
      {{true, RecoveryImageState::PendingVerify, true, false, true, false},
       RecoveryDiagnostic::OtaPendingVerify},
      {{true, RecoveryImageState::Valid, true, false, true, false},
       RecoveryDiagnostic::OtaPendingVerify},
      {{true, RecoveryImageState::Invalid, false, false, true, false},
       RecoveryDiagnostic::OtaImageInvalid},
      {{true, RecoveryImageState::Aborted, false, false, true, false},
       RecoveryDiagnostic::OtaImageAborted},
      {{true, RecoveryImageState::Unknown, false, false, true, false},
       RecoveryDiagnostic::OtaImageUnknown},
      {{false, RecoveryImageState::Valid, false, false, true, false},
       RecoveryDiagnostic::OtaStatusReadError},
      {{true, RecoveryImageState::Valid, false, true, true, false},
       RecoveryDiagnostic::OtaUploadActive},
      {{true, RecoveryImageState::Valid, false, false, false, false},
       RecoveryDiagnostic::ConfigStatusReadError},
      {{true, RecoveryImageState::Valid, false, false, true, true},
       RecoveryDiagnostic::ConfigRestoreActive},
  };
  for (const auto& denial : denials) {
    RecoveryStateMachine machine;
    reachOtaCheck(machine);
    const auto result = machine.evaluateGate(denial.conditions, 0);
    assert(result.to == RecoveryState::Failed);
    assert(result.diagnostic == denial.diagnostic);
  }

  RecoveryStateMachine flow;
  reachReady(flow, 10);
  assert(flow.dispatch(RecoveryEvent::Activate, 11).to ==
         RecoveryState::Active);
  assert(flow.dispatch(RecoveryEvent::RequireConfirmation, 12).to ==
         RecoveryState::AwaitConfirmation);
  assert(flow.dispatch(RecoveryEvent::Confirm, 13).to ==
         RecoveryState::Committing);
  assert(flow.dispatch(RecoveryEvent::CommitSucceeded, 14).to ==
         RecoveryState::Completed);

  RecoveryStateMachine commitFailure;
  reachReady(commitFailure);
  commitFailure.dispatch(RecoveryEvent::Activate, 1);
  commitFailure.dispatch(RecoveryEvent::RequireConfirmation, 2);
  commitFailure.dispatch(RecoveryEvent::Confirm, 3);
  assert(commitFailure.dispatch(RecoveryEvent::CommitFailed, 4).diagnostic ==
         RecoveryDiagnostic::CommitFailed);

  RecoveryStateMachine committingTimeout;
  reachReady(committingTimeout);
  committingTimeout.dispatch(RecoveryEvent::Activate, 1);
  committingTimeout.dispatch(RecoveryEvent::RequireConfirmation, 2);
  committingTimeout.dispatch(RecoveryEvent::Confirm, 3);
  assert(committingTimeout.dispatch(RecoveryEvent::Cancel, 4).diagnostic ==
         RecoveryDiagnostic::InvalidEvent);
  assert(committingTimeout.dispatch(RecoveryEvent::CommitSucceeded,
                                    600000U).diagnostic ==
         RecoveryDiagnostic::SessionTimeout);

  RecoveryStateMachine cancellation;
  reachReady(cancellation);
  assert(cancellation.dispatch(RecoveryEvent::Cancel, 1).to ==
         RecoveryState::Cancelled);

  RecoveryStateMachine internalFailure;
  reachReady(internalFailure);
  assert(internalFailure.dispatch(RecoveryEvent::InternalFailure, 1).to ==
         RecoveryState::Failed);

  RecoveryStateMachine sessionTimeout;
  reachReady(sessionTimeout, 20);
  assert(sessionTimeout.dispatch(RecoveryEvent::Tick, 20 + 599999U).to ==
         RecoveryState::Ready);
  assert(sessionTimeout.dispatch(RecoveryEvent::Tick, 20 + 600000U).diagnostic ==
         RecoveryDiagnostic::SessionTimeout);

  RecoveryStateMachine confirmationTimeout;
  reachReady(confirmationTimeout);
  confirmationTimeout.dispatch(RecoveryEvent::Activate, 1);
  confirmationTimeout.dispatch(RecoveryEvent::RequireConfirmation, 100);
  assert(confirmationTimeout.dispatch(RecoveryEvent::Tick, 30099).to ==
         RecoveryState::AwaitConfirmation);
  assert(confirmationTimeout.dispatch(RecoveryEvent::Tick, 30100).diagnostic ==
         RecoveryDiagnostic::ConfirmationTimeout);

  RecoveryStateMachine wrap;
  const uint32_t start = UINT32_MAX - 100U;
  reachReady(wrap, start);
  wrap.dispatch(RecoveryEvent::Activate, start + 1U);
  wrap.dispatch(RecoveryEvent::RequireConfirmation, start + 2U);
  assert(wrap.dispatch(RecoveryEvent::Tick, start + 30001U).to ==
         RecoveryState::AwaitConfirmation);
  assert(wrap.dispatch(RecoveryEvent::Tick, start + 30002U).diagnostic ==
         RecoveryDiagnostic::ConfirmationTimeout);

  RecoveryStateMachine invalid;
  assert(invalid.dispatch(RecoveryEvent::Activate, 0).diagnostic ==
         RecoveryDiagnostic::InvalidEvent);
  assert(invalid.evaluateGate(eligible(), 0).diagnostic ==
         RecoveryDiagnostic::InvalidEvent);
  assert(invalid.state() == RecoveryState::Idle);

  const auto& hardware = voxone::hardware::currentHardware();
#if defined(VOXONE_PROFILE_W0)
  assert(hardware.localInput.key == 32);
#elif defined(VOXONE_PROFILE_L0)
  assert(hardware.localInput.key == 39);
#else
  assert(hardware.localInput.key == 4);
#endif

  const std::string core = readFile("src/core/security_recovery_state.cpp");
  const std::string runtime =
      readFile("src/core/security_recovery_state_runtime.cpp");
  const std::string combined = core + runtime;
  assert(core.find("new ") == std::string::npos);
  assert(core.find("xTaskCreate") == std::string::npos);
  assert(combined.find("softAP") == std::string::npos);
  assert(combined.find("AsyncWebServer") == std::string::npos);
  assert(combined.find("Preferences") == std::string::npos);
  assert(combined.find("nvs_") == std::string::npos);
  assert(combined.find("esp_ota_mark_app_valid") == std::string::npos);
  assert(combined.find("esp_ota_mark_app_invalid") == std::string::npos);
  assert(combined.find("ESP.restart") == std::string::npos);
  assert(runtime.find("runningImageSnapshot()") != std::string::npos);
  assert(runtime.find("updateLockActive()") != std::string::npos);
  assert(runtime.find("inspectBootRecovery()") != std::string::npos);
  assert(runtime.find("bootHealthReady()") == std::string::npos);
  assert(runtime.find("/api/ready") == std::string::npos);

  const std::string main = readFile("src/main.cpp");
  const size_t health = main.find("beginBootHealthMonitor()");
  const size_t key = main.find("detectBootKeyGesture()");
  const size_t gate = main.find("beginRecoverySession(");
  const size_t restore = main.find("recoverAtBoot()");
  assert(health < key && key < gate && gate < restore);
}
