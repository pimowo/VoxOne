#ifndef VOXONE_SECURITY_RECOVERY_H
#define VOXONE_SECURITY_RECOVERY_H

// Consolidated from security_recovery_key.h.
#include <stdint.h>

namespace voxone {
namespace security_recovery {

enum class KeyState : uint8_t {
  Idle,
  Debounce,
  Accepted,
  Cancelled,
};

struct KeyTimings {
  uint32_t debounceMs;
};

constexpr KeyTimings kDefaultKeyTimings = {50U};

class BootKeyDetector {
 public:
  explicit BootKeyDetector(KeyTimings timings = kDefaultKeyTimings);

  KeyState begin(bool keyLow, uint32_t nowMs);
  KeyState update(bool keyLow, uint32_t nowMs);

  KeyState state() const { return state_; }
  bool finished() const;

 private:
  void enter(KeyState state, uint32_t nowMs);
  bool elapsed(uint32_t nowMs, uint32_t intervalMs) const;

  KeyTimings timings_;
  KeyState state_;
  uint32_t stateStartedMs_;
};

}  // namespace security_recovery
}  // namespace voxone

// Consolidated from security_recovery_state.h.
#include <stdint.h>

namespace voxone {
namespace security_recovery {

enum class RecoveryState : uint8_t { Idle, Requested, Active, Committing, Completed, Failed };
enum class RecoveryEvent : uint8_t { Activate, BeginCommit, CommitSucceeded, CommitFailed, InternalFailure };
enum class RecoveryDiagnostic : uint8_t {
  None, KeyNotAccepted, DuplicateRequest, Requested, Activated, Committing,
  Completed, CommitFailed, InternalFailure, InvalidEvent,
};

struct RecoveryTransition {
  RecoveryState from;
  RecoveryState to;
  RecoveryDiagnostic diagnostic;
  bool changed;
};

class RecoveryStateMachine {
 public:
  RecoveryTransition observeBootKey(KeyState keyState);
  RecoveryTransition dispatch(RecoveryEvent event);
  RecoveryState state() const { return state_; }
  RecoveryDiagnostic diagnostic() const { return diagnostic_; }
  bool requestedThisBoot() const { return requestConsumed_; }

 private:
  RecoveryTransition transition(RecoveryState state, RecoveryDiagnostic diagnostic);
  RecoveryTransition report(RecoveryDiagnostic diagnostic);
  RecoveryState state_ = RecoveryState::Idle;
  RecoveryDiagnostic diagnostic_ = RecoveryDiagnostic::None;
  bool bootKeyObserved_ = false;
  bool requestConsumed_ = false;
};

const char* recoveryStateName(RecoveryState state);
const char* recoveryDiagnosticName(RecoveryDiagnostic diagnostic);

}  // namespace security_recovery
}  // namespace voxone

// Consolidated from security_recovery_session.h.
namespace voxone { namespace security_recovery {
class RecoverySession {
 public:
  RecoveryTransition begin(KeyState keyState);
  RecoveryTransition activate();
  RecoveryTransition beginCommit();
  RecoveryTransition complete(bool success);
  RecoveryTransition fail();
  RecoveryTransition current() const;
  RecoveryState state() const { return machine_.state(); }
  RecoveryDiagnostic diagnostic() const { return machine_.diagnostic(); }
  bool requestedThisBoot() const { return machine_.requestedThisBoot(); }
  bool live() const;
  bool activated() const;
 private:
  RecoveryStateMachine machine_;
};
} }

// Consolidated from security_recovery_ap.h.
namespace voxone {
namespace security_recovery {

enum class RecoveryApStartResult : uint8_t {
  NotRequested,
  WifiFailed,
  HttpFailed,
  Active,
};

class RecoveryApSessionControl {
 public:
  virtual ~RecoveryApSessionControl() {}
  virtual RecoveryState state() const = 0;
  virtual bool live() const = 0;
  virtual RecoveryTransition activate() = 0;
  virtual RecoveryTransition fail() = 0;
  virtual RecoveryTransition complete(bool success) = 0;
};

class RecoveryApPlatform {
 public:
  virtual ~RecoveryApPlatform() {}
  virtual bool startWifi() = 0;
  virtual bool startHttp() = 0;
  virtual bool serviceHttp(uint32_t nowMs) = 0;
  virtual void stopHttp() = 0;
  virtual void disconnectClient() = 0;
  virtual void stopWifi() = 0;
  virtual void clearTemporary() = 0;
  virtual void restart() = 0;
};

class RecoveryApController {
 public:
  RecoveryApController(RecoveryApSessionControl& session,
                       RecoveryApPlatform& platform)
      : session_(session), platform_(platform) {}

  RecoveryApStartResult start(uint32_t nowMs);
  bool service(uint32_t nowMs);
  void complete(bool success);

  bool ownsRuntime() const { return ownsRuntime_; }
  bool running() const { return running_; }

 private:
  void cleanup();

  RecoveryApSessionControl& session_;
  RecoveryApPlatform& platform_;
  bool ownsRuntime_ = false;
  bool running_ = false;
  bool cleaned_ = true;
  bool restartIssued_ = false;
};

const char* recoveryApStartResultName(RecoveryApStartResult result);

}  // namespace security_recovery
}  // namespace voxone

// Consolidated from security_recovery_http.h.
#include <stddef.h>
#include <stdint.h>
namespace voxone { namespace security_recovery {
// A trusted browser form navigation includes Sec-Fetch-User and can exceed
// 768 bytes even for the minimal Recovery page. Keep a bounded 1 KiB header.
constexpr size_t kRecoveryHttpMaxHeaderBytes = 1024U;
constexpr size_t kRecoveryHttpMaxBodyBytes = 256U;
constexpr size_t kRecoveryHttpMaxRequestBytes = kRecoveryHttpMaxHeaderBytes + kRecoveryHttpMaxBodyBytes;
constexpr uint32_t kRecoveryHttpClientTimeoutMs = 2000U;
constexpr uint16_t kRecoveryHttpMaxRequests = 64U;
constexpr size_t kRecoveryCsrfCharacters = 48U;
enum class RecoveryHttpFraming : uint8_t { Incomplete, Complete, Invalid, TooLarge };
enum class RecoveryHttpDecision : uint8_t {
  Root, Password, FactoryReset, BadRequest, HostRejected, OriginRejected,
  UnsupportedMediaType, PayloadTooLarge, MethodNotAllowed, NotFound, RateLimited,
};
struct RecoveryHttpRequest { RecoveryHttpDecision decision; size_t bodyOffset; size_t bodyLength; };
RecoveryHttpFraming inspectRecoveryHttpFraming(const char* request, size_t length, size_t& expectedLength);
RecoveryHttpDecision evaluateRecoveryHttpRequest(const char* request, size_t length,
                                                 const char* expectedHost,
                                                 bool requestAllowed,
                                                 RecoveryHttpRequest& parsed);
int renderRecoveryRootPage(const char* csrf, char* output, size_t capacity);
bool parseRecoveryPasswordForm(const char* body, size_t length,
                               const char* expectedCsrf, char* password,
                               size_t passwordCapacity);
bool parseRecoveryCsrfForm(const char* body, size_t length,
                           const char* expectedCsrf);
const char* recoveryHttpDecisionName(RecoveryHttpDecision decision);
} }

// Consolidated from security_recovery_key_runtime.h.
namespace voxone {
namespace security_recovery {

// Samples the active-low KEY once at early boot with only contact debounce.
// The accepted decision is latched for this boot; release is never required.
KeyState detectBootKeyAtStartup();

// Used by the one-shot post-factory-reset FIRST BOOT bypass so a still-held KEY
// cannot become a synthetic runtime click. Encoder A/B remain operational.
void suppressRuntimeKeyUntilRelease();
bool runtimeKeyInputReady();

}  // namespace security_recovery
}  // namespace voxone

// Consolidated from security_recovery_session_runtime.h.
namespace voxone { namespace security_recovery {
struct RecoverySessionSnapshot { RecoveryState state; RecoveryDiagnostic diagnostic; bool requested; bool live; bool activated; };
RecoverySessionSnapshot beginRecoverySession(KeyState keyState);
RecoverySessionSnapshot recoverySessionSnapshot();
RecoveryTransition activateRecoverySession();
RecoveryTransition beginRecoveryCommit();
RecoveryTransition failRecoverySession();
RecoveryTransition completeRecoverySession(bool success);
} }

// Consolidated from security_recovery_ap_runtime.h.
namespace voxone {
namespace security_recovery {

RecoveryApStartResult beginSecurityRecoveryAp(uint32_t nowMs);
bool serviceSecurityRecoveryAp(uint32_t nowMs);
bool securityRecoveryApOwnsRuntime();
bool securityRecoveryApRunning();

}  // namespace security_recovery
}  // namespace voxone

#endif  // VOXONE_SECURITY_RECOVERY_H
