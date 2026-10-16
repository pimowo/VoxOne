#ifndef VOXONE_SECURITY_RECOVERY_PIN_H
#define VOXONE_SECURITY_RECOVERY_PIN_H

#include <stddef.h>
#include <stdint.h>

#include "security_settings.h"

namespace voxone {
namespace security_recovery {

constexpr uint8_t kRecoveryPinMaxAttempts = 5U;
constexpr uint32_t kRecoveryPinRetryDelayMs = 1000U;
constexpr size_t kRecoveryTokenBytes = 32U;
constexpr size_t kRecoveryTokenCharacters = kRecoveryTokenBytes * 2U;
constexpr size_t kRecoveryTokenBufferSize = kRecoveryTokenCharacters + 1U;
constexpr size_t kRecoveryFormMaxBodyBytes = 512U;

class RecoveryRandomSource {
 public:
  virtual ~RecoveryRandomSource() {}
  virtual bool fill(uint8_t* output, size_t size) = 0;
};

struct RecoveryFormData {
  char pin[5];
  char password[security::kMaxAdminPasswordBytes + 1U];
  char repeatedPassword[security::kMaxAdminPasswordBytes + 1U];
  char csrf[kRecoveryTokenBufferSize];
};

struct PreparedRecoveryCredential {
  uint32_t iterations;
  uint8_t salt[security::kSecuritySaltSize];
  uint8_t verifier[security::kSecurityVerifierSize];
  bool valid;
};

enum class RecoveryPinResult : uint8_t {
  Accepted,
  InvalidPin,
  RetryLater,
  AttemptsExhausted,
  InvalidPassword,
  PasswordMismatch,
  InvalidSession,
  InvalidCsrf,
  Frozen,
  RandomFailure,
  DeriveFailure,
  MalformedForm,
};

bool parseRecoveryForm(const char* body, size_t length,
                       RecoveryFormData& output);
void clearRecoveryFormData(RecoveryFormData& data);

class RecoveryPinSession {
 public:
  explicit RecoveryPinSession(RecoveryRandomSource& random)
      : random_(random) {}

  bool begin();
  RecoveryPinResult submit(const char* sessionToken,
                           const RecoveryFormData& form,
                           uint32_t nowMs);
  void clear();

  const char* sessionToken() const { return sessionToken_; }
  const char* csrfToken() const { return csrfToken_; }
  const PreparedRecoveryCredential& candidate() const { return candidate_; }
  uint8_t failedAttempts() const { return failedAttempts_; }
  bool frozen() const { return frozen_; }
  bool locked() const { return locked_; }
  bool started() const { return started_; }

 private:
  bool retryDelayActive(uint32_t nowMs) const;
  RecoveryPinResult rejectPin(uint32_t nowMs);

  RecoveryRandomSource& random_;
  char sessionToken_[kRecoveryTokenBufferSize]{};
  char csrfToken_[kRecoveryTokenBufferSize]{};
  PreparedRecoveryCredential candidate_{};
  uint32_t retryAfterMs_ = 0;
  uint8_t failedAttempts_ = 0;
  bool retryDelaySet_ = false;
  bool frozen_ = false;
  bool locked_ = false;
  bool started_ = false;
};

const char* recoveryPinResultName(RecoveryPinResult result);

}  // namespace security_recovery
}  // namespace voxone

#endif
