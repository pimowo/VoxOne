#include "../src/core/security_recovery_pin.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

using namespace voxone::security_recovery;

namespace {

class FakeRandom : public RecoveryRandomSource {
 public:
  bool fail = false;
  uint8_t next = 1;

  bool fill(uint8_t* output, size_t size) override {
    if (fail) return false;
    for (size_t index = 0; index < size; ++index) output[index] = next++;
    return true;
  }
};

RecoveryFormData validForm(const RecoveryPinSession& session,
                           const char* password = "abcd") {
  RecoveryFormData form{};
  std::strcpy(form.pin, "1293");
  std::strcpy(form.password, password);
  std::strcpy(form.repeatedPassword, password);
  std::strcpy(form.csrf, session.csrfToken());
  return form;
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  FakeRandom random;
  RecoveryPinSession session(random);
  assert(session.begin());
  assert(!session.begin());
  assert(session.started());
  assert(std::strlen(session.sessionToken()) == kRecoveryTokenCharacters);
  assert(std::strlen(session.csrfToken()) == kRecoveryTokenCharacters);
  assert(std::strcmp(session.sessionToken(), session.csrfToken()) != 0);

  RecoveryFormData form = validForm(session);
  assert(session.submit("invalid", form, 0) ==
         RecoveryPinResult::InvalidSession);
  assert(session.failedAttempts() == 0);
  form.csrf[0] = form.csrf[0] == '0' ? '1' : '0';
  assert(session.submit(session.sessionToken(), form, 0) ==
         RecoveryPinResult::InvalidCsrf);
  assert(session.failedAttempts() == 0);

  form = validForm(session, "abc");
  assert(session.submit(session.sessionToken(), form, 0) ==
         RecoveryPinResult::InvalidPassword);
  form = validForm(session);
  std::strcpy(form.repeatedPassword, "abce");
  assert(session.submit(session.sessionToken(), form, 0) ==
         RecoveryPinResult::PasswordMismatch);

  form = validForm(session, "\xC4\x85\xC4\x99\xC5\xBA\xC4\x87");
  assert(session.submit(session.sessionToken(), form, 0) ==
         RecoveryPinResult::Accepted);
  assert(session.frozen());
  assert(session.candidate().valid);
  assert(session.candidate().iterations ==
         voxone::security::kPbkdf2Iterations);
  assert(session.submit(session.sessionToken(), form, 1000) ==
         RecoveryPinResult::Frozen);
  clearRecoveryFormData(form);
  assert(form.pin[0] == '\0' && form.password[0] == '\0' &&
         form.repeatedPassword[0] == '\0' && form.csrf[0] == '\0');

  FakeRandom attemptRandom;
  RecoveryPinSession attempts(attemptRandom);
  assert(attempts.begin());
  RecoveryFormData wrong = validForm(attempts);
  std::strcpy(wrong.pin, "0000");
  assert(attempts.submit(attempts.sessionToken(), wrong, 0) ==
         RecoveryPinResult::InvalidPin);
  assert(attempts.submit(attempts.sessionToken(), wrong, 999) ==
         RecoveryPinResult::RetryLater);
  assert(attempts.failedAttempts() == 1);
  assert(!attempts.begin());
  for (uint8_t attempt = 2; attempt <= kRecoveryPinMaxAttempts; ++attempt) {
    const RecoveryPinResult result = attempts.submit(
        attempts.sessionToken(), wrong,
        static_cast<uint32_t>(attempt - 1U) * kRecoveryPinRetryDelayMs);
    assert(result == (attempt == kRecoveryPinMaxAttempts
                          ? RecoveryPinResult::AttemptsExhausted
                          : RecoveryPinResult::InvalidPin));
  }
  assert(attempts.locked());
  assert(attempts.failedAttempts() == kRecoveryPinMaxAttempts);
  assert(attempts.submit(attempts.sessionToken(), wrong, 10000) ==
         RecoveryPinResult::AttemptsExhausted);

  RecoveryFormData parsed{};
  const char encoded[] =
      "pin=1293&new_password=%C4%85%C4%99%C5%BA%C4%87&"
      "repeat_password=%C4%85%C4%99%C5%BA%C4%87&csrf=abc123";
  assert(parseRecoveryForm(encoded, std::strlen(encoded), parsed));
  assert(std::strcmp(parsed.pin, "1293") == 0);
  assert(std::strcmp(parsed.password,
                     "\xC4\x85\xC4\x99\xC5\xBA\xC4\x87") == 0);
  assert(std::strcmp(parsed.csrf, "abc123") == 0);
  clearRecoveryFormData(parsed);
  const char duplicate[] =
      "pin=1293&pin=1293&new_password=abcd&repeat_password=abcd&csrf=x";
  assert(!parseRecoveryForm(duplicate, std::strlen(duplicate), parsed));
  const char embeddedNul[] =
      "pin=1293&new_password=ab%00cd&repeat_password=abcd&csrf=x";
  assert(!parseRecoveryForm(embeddedNul, std::strlen(embeddedNul), parsed));
  const std::string longPassword(65, 'a');
  const std::string oversizedField =
      "pin=1293&new_password=" + longPassword +
      "&repeat_password=" + longPassword + "&csrf=x";
  assert(!parseRecoveryForm(oversizedField.data(), oversizedField.size(),
                            parsed));

  char maxPassword[voxone::security::kMaxAdminPasswordBytes + 1U];
  std::memset(maxPassword, 'a', sizeof(maxPassword) - 1U);
  maxPassword[sizeof(maxPassword) - 1U] = '\0';
  size_t bytes = 0;
  size_t characters = 0;
  assert(voxone::security::securityValidateAdminPassword(
      maxPassword, &bytes, &characters));
  assert(bytes == 64U && characters == 64U);
  const char invalidUtf8[] = {'a', 'b', 'c', static_cast<char>(0xc0), '\0'};
  assert(!voxone::security::securityValidateAdminPassword(invalidUtf8));

  session.clear();
  assert(!session.started() && !session.frozen() && !session.locked());
  assert(session.sessionToken()[0] == '\0' && session.csrfToken()[0] == '\0');
  assert(!session.candidate().valid);

  FakeRandom failedRandom;
  failedRandom.fail = true;
  RecoveryPinSession failed(failedRandom);
  assert(!failed.begin());
  assert(!failed.started());

  const std::string source =
      readFile("src/core/security_recovery_pin.cpp");
  assert(source.find("\"1293\"") != std::string::npos);
  assert(source.find("Serial") == std::string::npos);
  assert(source.find("printf") == std::string::npos);
  assert(source.find("nvs_") == std::string::npos);
  assert(source.find("Preferences") == std::string::npos);
  assert(source.find("SPIFFS") == std::string::npos);
  assert(source.find("ESP.restart") == std::string::npos);
}
