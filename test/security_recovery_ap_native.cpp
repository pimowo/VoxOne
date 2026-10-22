#include "../src/core/security_recovery_ap.h"
#include "../src/core/security_recovery_http.h"
#include "../src/core/security_settings.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string>

using namespace voxone::security_recovery;

namespace {

constexpr char kHost[] = "192.168.4.1";
constexpr char kCsrf[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef";

class Session : public RecoveryApSessionControl {
 public:
  RecoveryState value = RecoveryState::Requested;

  bool live() const override {
    return value == RecoveryState::Requested || value == RecoveryState::Active ||
           value == RecoveryState::Committing;
  }
  RecoveryState state() const override { return value; }
  RecoveryTransition activate() override {
    const auto from = value;
    value = RecoveryState::Active;
    return {from, value, RecoveryDiagnostic::Activated, true};
  }
  RecoveryTransition fail() override {
    const auto from = value;
    value = RecoveryState::Failed;
    return {from, value, RecoveryDiagnostic::InternalFailure, true};
  }
  RecoveryTransition complete(bool ok) override {
    const auto from = value;
    value = ok ? RecoveryState::Completed : RecoveryState::Failed;
    return {from, value,
            ok ? RecoveryDiagnostic::Completed
               : RecoveryDiagnostic::CommitFailed,
            true};
  }
};

class Platform : public RecoveryApPlatform {
 public:
  bool wifi = true;
  bool http = true;
  bool serving = true;
  bool restarted = false;

  bool startWifi() override { return wifi; }
  bool startHttp() override { return http; }
  bool serviceHttp(uint32_t) override { return serving; }
  void stopHttp() override {}
  void disconnectClient() override {}
  void stopWifi() override {}
  void clearTemporary() override {}
  void restart() override { restarted = true; }
};

RecoveryHttpDecision decision(const std::string& request,
                              RecoveryHttpRequest* output = nullptr) {
  RecoveryHttpRequest parsed{};
  const RecoveryHttpDecision result = evaluateRecoveryHttpRequest(
      request.data(), request.size(), kHost, true, parsed);
  if (output) *output = parsed;
  return result;
}

std::string browserPasswordRequest(const std::string& body,
                                   const char* host = kHost,
                                   const char* origin = "http://192.168.4.1",
                                   const char* contentType =
                                       "application/x-www-form-urlencoded") {
  std::string request = "POST /password HTTP/1.1\r\nHost: ";
  request += host;
  request += "\r\nConnection: keep-alive\r\nContent-Length: ";
  request += std::to_string(body.size());
  request +=
      "\r\nCache-Control: max-age=0"
      "\r\nsec-ch-ua: \"Chromium\";v=\"141\", \"Not_A Brand\";v=\"99\""
      "\r\nsec-ch-ua-mobile: ?0"
      "\r\nsec-ch-ua-platform: \"Linux\""
      "\r\nOrigin: ";
  request += origin;
  request += "\r\nContent-Type: ";
  request += contentType;
  request +=
      "\r\nUpgrade-Insecure-Requests: 1"
      "\r\nUser-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/141.0.0.0 Safari/537.36"
      "\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,"
      "image/avif,image/webp,image/apng,*/*;q=0.8,"
      "application/signed-exchange;v=b3;q=0.7"
      "\r\nSec-Fetch-Site: same-origin"
      "\r\nSec-Fetch-Mode: navigate"
      "\r\nSec-Fetch-User: ?1"
      "\r\nSec-Fetch-Dest: document"
      "\r\nAccept-Encoding: gzip, deflate"
      "\r\nAccept-Language: pl-PL,pl;q=0.9,en-US;q=0.8,en;q=0.7"
      "\r\n\r\n";
  request += body;
  return request;
}

void testControllerRestartPolicy() {
  Session successSession;
  Platform successPlatform;
  RecoveryApController success(successSession, successPlatform);
  assert(success.start(0) == RecoveryApStartResult::Active);
  assert(success.running() && success.ownsRuntime());
  assert(success.service(1));
  assert(!successPlatform.restarted);
  successSession.complete(true);
  assert(success.service(2));
  assert(successPlatform.restarted);

  Session failedSession;
  Platform failedPlatform;
  RecoveryApController failed(failedSession, failedPlatform);
  assert(failed.start(0) == RecoveryApStartResult::Active);
  failedSession.complete(false);
  assert(failed.service(1));
  assert(!failedPlatform.restarted);

  Session invalidSession;
  Platform invalidPlatform;
  RecoveryApController invalid(invalidSession, invalidPlatform);
  assert(invalid.start(0) == RecoveryApStartResult::Active);
  assert(invalid.service(1));
  assert(!invalidPlatform.restarted);
  assert(invalidSession.state() == RecoveryState::Active);
}

void testProductionFormThroughHttpPath() {
  char page[2300];
  const int pageLength = renderRecoveryRootPage(kCsrf, page, sizeof(page));
  assert(pageLength > 0 && static_cast<size_t>(pageLength) < sizeof(page));
  const std::string html(page, static_cast<size_t>(pageLength));
  assert(html.find("<form method=\"post\" action=\"/password\">") !=
         std::string::npos);
  assert(html.find("name=\"csrf\" value=\"" + std::string(kCsrf) +
                   "\"") != std::string::npos);
  assert(html.find("name=\"new_password\" type=\"password\"") !=
         std::string::npos);

  const std::string body =
      "csrf=" + std::string(kCsrf) + "&new_password=Poprawne+haslo-123";
  const std::string request = browserPasswordRequest(body);
  const size_t headerBytes = request.find("\r\n\r\n") + 4U;
  assert(headerBytes > 768U);
  assert(headerBytes <= kRecoveryHttpMaxHeaderBytes);

  RecoveryHttpRequest parsed{};
  assert(decision(request, &parsed) == RecoveryHttpDecision::Password);
  assert(parsed.bodyLength == body.size());
  char password[voxone::security::kMaxAdminPasswordBytes + 1U]{};
  assert(parseRecoveryPasswordForm(request.data() + parsed.bodyOffset,
                                   parsed.bodyLength, kCsrf, password,
                                   sizeof(password)));
  assert(std::strcmp(password, "Poprawne haslo-123") == 0);
  assert(voxone::security::securityValidateAdminPassword(password));
}

void testRejectedRequestsAndFormValidation() {
  const std::string validBody =
      "csrf=" + std::string(kCsrf) + "&new_password=Poprawne-haslo-123";
  assert(decision(browserPasswordRequest(validBody, "evil.invalid")) ==
         RecoveryHttpDecision::HostRejected);
  assert(decision(browserPasswordRequest(validBody, kHost,
                                         "http://evil.invalid")) ==
         RecoveryHttpDecision::OriginRejected);
  assert(decision(browserPasswordRequest(validBody, kHost,
                                         "http://192.168.4.1",
                                         "text/plain")) ==
         RecoveryHttpDecision::UnsupportedMediaType);

  std::string tooLarge = "GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nX-Fill: ";
  tooLarge.append(kRecoveryHttpMaxHeaderBytes, 'x');
  assert(decision(tooLarge) == RecoveryHttpDecision::PayloadTooLarge);

  std::string oversizedBody(kRecoveryHttpMaxBodyBytes + 1U, 'a');
  assert(decision(browserPasswordRequest(oversizedBody)) ==
         RecoveryHttpDecision::PayloadTooLarge);

  char password[voxone::security::kMaxAdminPasswordBytes + 1U]{};
  const std::string wrongCsrf =
      "csrf=ffffffffffffffffffffffffffffffffffffffffffffffff"
      "&new_password=Poprawne-haslo-123";
  assert(!parseRecoveryPasswordForm(wrongCsrf.data(), wrongCsrf.size(),
                                    kCsrf, password, sizeof(password)));
  const std::string wrongField =
      "csrf=" + std::string(kCsrf) + "&password=Poprawne-haslo-123";
  assert(!parseRecoveryPasswordForm(wrongField.data(), wrongField.size(),
                                    kCsrf, password, sizeof(password)));
  const std::string duplicate =
      "csrf=" + std::string(kCsrf) + "&csrf=" + std::string(kCsrf) +
      "&new_password=Poprawne-haslo-123";
  assert(!parseRecoveryPasswordForm(duplicate.data(), duplicate.size(),
                                    kCsrf, password, sizeof(password)));
  const std::string malformed =
      "csrf=" + std::string(kCsrf) + "&new_password=%GG";
  assert(!parseRecoveryPasswordForm(malformed.data(), malformed.size(),
                                    kCsrf, password, sizeof(password)));

  const std::string tooShort =
      "csrf=" + std::string(kCsrf) + "&new_password=abc";
  assert(parseRecoveryPasswordForm(tooShort.data(), tooShort.size(), kCsrf,
                                   password, sizeof(password)));
  assert(!voxone::security::securityValidateAdminPassword(password));

  const std::string invalidUtf8 =
      "csrf=" + std::string(kCsrf) + "&new_password=abc%80";
  assert(parseRecoveryPasswordForm(invalidUtf8.data(), invalidUtf8.size(),
                                   kCsrf, password, sizeof(password)));
  assert(!voxone::security::securityValidateAdminPassword(password));

  const std::string validUtf8 =
      "csrf=" + std::string(kCsrf) +
      "&new_password=za%C5%BC%C3%B3%C5%82%C4%87-123";
  assert(parseRecoveryPasswordForm(validUtf8.data(), validUtf8.size(), kCsrf,
                                   password, sizeof(password)));
  assert(voxone::security::securityValidateAdminPassword(password));

  const std::string factoryBody = "csrf=" + std::string(kCsrf);
  assert(parseRecoveryCsrfForm(factoryBody.data(), factoryBody.size(), kCsrf));
  assert(!parseRecoveryCsrfForm(validBody.data(), validBody.size(), kCsrf));
}

void testBasicRoutes() {
  assert(decision("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n") ==
         RecoveryHttpDecision::Root);
  const std::string factory =
      "POST /factory-reset HTTP/1.1\r\nHost: 192.168.4.1\r\n"
      "Origin: http://192.168.4.1\r\n"
      "Content-Type: application/x-www-form-urlencoded\r\n"
      "Content-Length: 3\r\n\r\na=b";
  assert(decision(factory) == RecoveryHttpDecision::FactoryReset);
  assert(decision("GET /api/ready HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n") ==
         RecoveryHttpDecision::NotFound);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--render-browser-fixture") == 0) {
    char page[2300];
    const int length = renderRecoveryRootPage(kCsrf, page, sizeof(page));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(page)) return 2;
    std::cout.write(page, length);
    return std::cout.good() ? 0 : 3;
  }
  if (argc == 2 && std::strcmp(argv[1], "--check-browser-request") == 0) {
    const std::string request((std::istreambuf_iterator<char>(std::cin)),
                              std::istreambuf_iterator<char>());
    RecoveryHttpRequest parsed{};
    const RecoveryHttpDecision route = decision(request, &parsed);
    if (route != RecoveryHttpDecision::Password) {
      std::cerr << recoveryHttpDecisionName(route) << '\n';
      return 2;
    }
    char password[voxone::security::kMaxAdminPasswordBytes + 1U]{};
    if (!parseRecoveryPasswordForm(request.data() + parsed.bodyOffset,
                                   parsed.bodyLength, kCsrf, password,
                                   sizeof(password)))
      return 3;
    return voxone::security::securityValidateAdminPassword(password) ? 0 : 4;
  }
  testControllerRestartPolicy();
  testProductionFormThroughHttpPath();
  testRejectedRequestsAndFormValidation();
  testBasicRoutes();
  return 0;
}
