#include "security_recovery_pin.h"

#include <cstring>

namespace voxone {
namespace security_recovery {
namespace {

constexpr char kRecoveryPin[] = "1293";

void secureZero(void* memory, size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(memory);
  while (size--) *bytes++ = 0;
}

uint8_t hexValue(char value) {
  if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<uint8_t>(value - 'a' + 10);
  if (value >= 'A' && value <= 'F')
    return static_cast<uint8_t>(value - 'A' + 10);
  return 0xffU;
}

bool decodeFormComponent(const char* input, size_t length, char* output,
                         size_t capacity) {
  if (!input || !output || !capacity) return false;
  size_t written = 0;
  for (size_t index = 0; index < length; ++index) {
    uint8_t value = static_cast<uint8_t>(input[index]);
    if (value == '+') {
      value = ' ';
    } else if (value == '%') {
      if (index + 2U >= length) return false;
      const uint8_t high = hexValue(input[index + 1U]);
      const uint8_t low = hexValue(input[index + 2U]);
      if (high == 0xffU || low == 0xffU) return false;
      value = static_cast<uint8_t>((high << 4U) | low);
      index += 2U;
    }
    if (!value || written + 1U >= capacity) return false;
    output[written++] = static_cast<char>(value);
  }
  output[written] = '\0';
  return true;
}

bool constantTimeEqual(const char* left, const char* right, size_t capacity) {
  if (!left || !right || !capacity) return false;
  uint8_t difference = 0;
  bool leftEnded = false;
  bool rightEnded = false;
  for (size_t index = 0; index < capacity; ++index) {
    const uint8_t a = leftEnded ? 0U : static_cast<uint8_t>(left[index]);
    const uint8_t b = rightEnded ? 0U : static_cast<uint8_t>(right[index]);
    difference |= static_cast<uint8_t>(a ^ b);
    leftEnded = leftEnded || a == 0U;
    rightEnded = rightEnded || b == 0U;
  }
  return difference == 0U && leftEnded && rightEnded;
}

bool pinMatches(const char* pin) {
  if (!pin) return false;
  uint8_t difference = 0;
  bool validFormat = true;
  for (size_t index = 0; index < 4U; ++index) {
    const uint8_t value = static_cast<uint8_t>(pin[index]);
    validFormat = validFormat && value >= '0' && value <= '9';
    difference |= static_cast<uint8_t>(value ^ kRecoveryPin[index]);
  }
  validFormat = validFormat && pin[4] == '\0';
  return validFormat && difference == 0U;
}

void encodeHex(const uint8_t* input, size_t size, char* output) {
  static const char digits[] = "0123456789abcdef";
  for (size_t index = 0; index < size; ++index) {
    output[index * 2U] = digits[input[index] >> 4U];
    output[index * 2U + 1U] = digits[input[index] & 0x0fU];
  }
  output[size * 2U] = '\0';
}

bool assignField(const char* name, const char* valueStart, size_t valueLength,
                 RecoveryFormData& output, uint8_t& fields) {
  uint8_t bit = 0;
  char* destination = nullptr;
  size_t capacity = 0;
  if (std::strcmp(name, "pin") == 0) {
    bit = 1U;
    destination = output.pin;
    capacity = sizeof(output.pin);
  } else if (std::strcmp(name, "new_password") == 0) {
    bit = 2U;
    destination = output.password;
    capacity = sizeof(output.password);
  } else if (std::strcmp(name, "repeat_password") == 0) {
    bit = 4U;
    destination = output.repeatedPassword;
    capacity = sizeof(output.repeatedPassword);
  } else if (std::strcmp(name, "csrf") == 0) {
    bit = 8U;
    destination = output.csrf;
    capacity = sizeof(output.csrf);
  } else {
    return false;
  }
  if (fields & bit) return false;
  if (!decodeFormComponent(valueStart, valueLength, destination, capacity))
    return false;
  fields |= bit;
  return true;
}

}  // namespace

bool parseRecoveryForm(const char* body, size_t length,
                       RecoveryFormData& output) {
  clearRecoveryFormData(output);
  if (!body || !length || length > kRecoveryFormMaxBodyBytes) return false;
  uint8_t fields = 0;
  size_t cursor = 0;
  while (cursor < length) {
    size_t end = cursor;
    while (end < length && body[end] != '&') ++end;
    size_t equals = cursor;
    while (equals < end && body[equals] != '=') ++equals;
    if (equals == cursor || equals == end || equals - cursor >= 24U) {
      clearRecoveryFormData(output);
      return false;
    }
    char name[24]{};
    if (!decodeFormComponent(body + cursor, equals - cursor, name,
                             sizeof(name)) ||
        !assignField(name, body + equals + 1U, end - equals - 1U, output,
                     fields)) {
      secureZero(name, sizeof(name));
      clearRecoveryFormData(output);
      return false;
    }
    secureZero(name, sizeof(name));
    if (end == length) break;
    cursor = end + 1U;
    if (cursor == length) {
      clearRecoveryFormData(output);
      return false;
    }
  }
  if (fields != 0x0fU) {
    clearRecoveryFormData(output);
    return false;
  }
  return true;
}

void clearRecoveryFormData(RecoveryFormData& data) {
  secureZero(&data, sizeof(data));
}

bool RecoveryPinSession::begin() {
  if (started_) return false;
  uint8_t random[kRecoveryTokenBytes];
  if (!random_.fill(random, sizeof(random))) {
    secureZero(random, sizeof(random));
    clear();
    return false;
  }
  encodeHex(random, sizeof(random), sessionToken_);
  if (!random_.fill(random, sizeof(random))) {
    secureZero(random, sizeof(random));
    clear();
    return false;
  }
  encodeHex(random, sizeof(random), csrfToken_);
  secureZero(random, sizeof(random));
  started_ = true;
  return true;
}

bool RecoveryPinSession::retryDelayActive(uint32_t nowMs) const {
  return retryDelaySet_ &&
         static_cast<int32_t>(nowMs - retryAfterMs_) < 0;
}

RecoveryPinResult RecoveryPinSession::rejectPin(uint32_t nowMs) {
  if (failedAttempts_ < kRecoveryPinMaxAttempts) ++failedAttempts_;
  if (failedAttempts_ >= kRecoveryPinMaxAttempts) {
    locked_ = true;
    return RecoveryPinResult::AttemptsExhausted;
  }
  retryAfterMs_ = nowMs + kRecoveryPinRetryDelayMs;
  retryDelaySet_ = true;
  return RecoveryPinResult::InvalidPin;
}

RecoveryPinResult RecoveryPinSession::submit(
    const char* sessionToken, const RecoveryFormData& form,
    uint32_t nowMs) {
  if (!started_ || locked_) return RecoveryPinResult::AttemptsExhausted;
  if (frozen_) return RecoveryPinResult::Frozen;
  if (!constantTimeEqual(sessionToken, sessionToken_,
                         kRecoveryTokenBufferSize))
    return RecoveryPinResult::InvalidSession;
  if (!constantTimeEqual(form.csrf, csrfToken_, kRecoveryTokenBufferSize))
    return RecoveryPinResult::InvalidCsrf;
  if (retryDelayActive(nowMs)) return RecoveryPinResult::RetryLater;
  retryDelaySet_ = false;
  if (!pinMatches(form.pin)) return rejectPin(nowMs);

  if (!constantTimeEqual(form.password, form.repeatedPassword,
                         security::kMaxAdminPasswordBytes + 1U))
    return RecoveryPinResult::PasswordMismatch;
  size_t passwordSize = 0;
  if (!security::securityValidateAdminPassword(form.password, &passwordSize))
    return RecoveryPinResult::InvalidPassword;

  PreparedRecoveryCredential prepared{};
  prepared.iterations = security::kPbkdf2Iterations;
  if (!random_.fill(prepared.salt, sizeof(prepared.salt))) {
    secureZero(&prepared, sizeof(prepared));
    return RecoveryPinResult::RandomFailure;
  }
  if (!security::securityPbkdf2HmacSha256(
          reinterpret_cast<const uint8_t*>(form.password), passwordSize,
          prepared.salt, sizeof(prepared.salt), prepared.iterations,
          prepared.verifier)) {
    secureZero(&prepared, sizeof(prepared));
    return RecoveryPinResult::DeriveFailure;
  }
  prepared.valid = true;
  candidate_ = prepared;
  secureZero(&prepared, sizeof(prepared));
  frozen_ = true;
  return RecoveryPinResult::Accepted;
}

void RecoveryPinSession::clear() {
  secureZero(sessionToken_, sizeof(sessionToken_));
  secureZero(csrfToken_, sizeof(csrfToken_));
  secureZero(&candidate_, sizeof(candidate_));
  retryAfterMs_ = 0;
  failedAttempts_ = 0;
  retryDelaySet_ = false;
  frozen_ = false;
  locked_ = false;
  started_ = false;
}

const char* recoveryPinResultName(RecoveryPinResult result) {
  switch (result) {
    case RecoveryPinResult::Accepted: return "accepted";
    case RecoveryPinResult::InvalidPin: return "invalid_pin";
    case RecoveryPinResult::RetryLater: return "retry_later";
    case RecoveryPinResult::AttemptsExhausted: return "attempts_exhausted";
    case RecoveryPinResult::InvalidPassword: return "invalid_password";
    case RecoveryPinResult::PasswordMismatch: return "password_mismatch";
    case RecoveryPinResult::InvalidSession: return "invalid_session";
    case RecoveryPinResult::InvalidCsrf: return "invalid_csrf";
    case RecoveryPinResult::Frozen: return "frozen";
    case RecoveryPinResult::RandomFailure: return "random_failure";
    case RecoveryPinResult::DeriveFailure: return "derive_failure";
    case RecoveryPinResult::MalformedForm: return "malformed_form";
  }
  return "unknown";
}

}  // namespace security_recovery
}  // namespace voxone
