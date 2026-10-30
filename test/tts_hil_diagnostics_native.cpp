#define VOXONE_TTS_HIL_DIAGNOSTICS 1
#include "../src/core/tts_hil_diagnostics.h"

#include <assert.h>

#include <atomic>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

using voxone::tts_hil::Diagnostics;
using voxone::tts_hil::EndReason;
using voxone::tts_hil::RunState;

static std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

static void testCompileFlagAndAuthenticatedRouteContract() {
  const std::string platform = readFile("platformio.ini");
  const size_t m0 = platform.find("[env:m0]");
  const size_t m0Next = platform.find("[env:m0-heap-trace]", m0);
  const size_t l0 = platform.find("[env:l0]");
  const size_t l0Next = platform.find("[env:l0-tx-only]", l0);
  assert(m0 != std::string::npos && m0Next != std::string::npos);
  assert(l0 != std::string::npos && l0Next != std::string::npos);
  assert(platform.substr(m0, m0Next - m0).find(
             "VOXONE_TTS_HIL_DIAGNOSTICS") == std::string::npos);
  assert(platform.substr(l0, l0Next - l0).find(
             "VOXONE_TTS_HIL_DIAGNOSTICS") == std::string::npos);
  assert(platform.find("[env:m0-tts-hil-diagnostics]") !=
         std::string::npos);
  assert(platform.find("[env:l0-tts-hil-diagnostics]") !=
         std::string::npos);

  const std::string server = readFile("src/core/netserver.cpp");
  const size_t handler = server.find(
      "void handleHardwareConfigRead(AsyncWebServerRequest* request)");
  const size_t handlerEnd = server.find(
      "void handleHardwareConfigSave(", handler);
  assert(handler != std::string::npos && handlerEnd != std::string::npos);
  const std::string handlerBody = server.substr(handler, handlerEnd - handler);
  const size_t authorization = handlerBody.find(
      "if (!deviceApiAuthorized(request)) return;");
  const size_t snapshotQuery = handlerBody.find("ttsDiagnostics");
  assert(authorization != std::string::npos);
  assert(snapshotQuery != std::string::npos && authorization < snapshotQuery);
  assert(server.find("webserver.on(\"/api/hardware\", HTTP_GET") !=
         std::string::npos);
  assert(server.find("webserver.on(\"/tts") == std::string::npos);

  const std::string security = readFile("src/core/security_access_policy.cpp");
  assert(security.find("ttsDiagnostics") == std::string::npos);
  assert(security.find("SecurityAccessMethod::Get, \"/api/hardware\"") !=
         std::string::npos);
}

static void testNaturalEofAndCleanup() {
  Diagnostics diagnostics;
  auto snapshot = diagnostics.snapshot();
  assert(snapshot.runId == 0);
  assert(snapshot.state == RunState::Idle);
  assert(snapshot.endReason == EndReason::None);

  diagnostics.start(41, 100, 1000, 500);
  diagnostics.observeProgress(41, 110, 1400, 900, 800);
  diagnostics.observeProgress(41, 111, 1400, 900, 800);
  diagnostics.naturalEof(41, 120, 1800, 1300, 800, true);
  diagnostics.cleanup(41, 130, 1800, 1300, 800);
  diagnostics.cleanup(41, 140, 1800, 1300, 800);

  snapshot = diagnostics.snapshot();
  assert(snapshot.runId == 41);
  assert(snapshot.state == RunState::Cleaned);
  assert(snapshot.endReason == EndReason::NaturalEof);
  assert(snapshot.bytesRead == 800);
  assert(snapshot.transportBytesRead == 800);
  assert(snapshot.httpContentLength == 800);
  assert(snapshot.startedAtMs == 100);
  assert(snapshot.lastProgressAtMs == 120);
  assert(snapshot.terminalAtMs == 120);
  assert(snapshot.cleanupAtMs == 130);
  assert(snapshot.events.starts == 1);
  assert(snapshot.events.progress == 2);
  assert(snapshot.events.naturalEofs == 1);
  assert(snapshot.events.terminals == 1);
  assert(snapshot.events.cleanups == 1);
  assert(snapshot.events.ignoredTerminalEvents == 0);
  assert(snapshot.totals.runs == 1);
  assert(snapshot.totals.naturalEofs == 1);
  assert(snapshot.totals.cleanups == 1);

  char json[Diagnostics::kJsonCapacity];
  assert(diagnostics.formatJson(json, sizeof(json)));
  assert(std::strstr(json, "\"runId\":41") != nullptr);
  assert(std::strstr(json, "\"state\":\"cleaned\"") != nullptr);
  assert(std::strstr(json, "\"endReason\":\"natural_eof\"") != nullptr);
  assert(std::strstr(json, "\"bytesRead\":800") != nullptr);
  assert(std::strstr(json, "\"httpBodyBytesRead\":800") != nullptr);
  assert(std::strstr(json, "\"httpContentLength\":800") != nullptr);
  assert(std::strstr(json, "\"transportBytesRead\":800") != nullptr);
}

static void testFirstTerminalReasonWins() {
  Diagnostics diagnostics;
  diagnostics.start(51, 200, 5000, 1000);
  diagnostics.observeProgress(51, 210, 6000, 2000, 3000);
  diagnostics.watchdogTimeout(51, 10210, 6000, 2000, 3000);
  diagnostics.naturalEof(51, 10220, 6000, 2000, 3000, false);
  diagnostics.cleanup(51, 10230, 6000, 2000, 3000);

  const auto snapshot = diagnostics.snapshot();
  assert(snapshot.runId == 51);
  assert(snapshot.state == RunState::Cleaned);
  assert(snapshot.endReason == EndReason::WatchdogTimeout);
  assert(snapshot.bytesRead == 1000);
  assert(snapshot.events.watchdogTimeouts == 1);
  assert(snapshot.events.naturalEofs == 1);
  assert(snapshot.events.terminals == 1);
  assert(snapshot.events.ignoredTerminalEvents == 1);
  assert(snapshot.events.cleanups == 1);
  assert(snapshot.totals.watchdogTimeouts == 1);
  assert(snapshot.totals.naturalEofs == 0);
}

static void testNaturalEofWinsLateDisconnectAndCleanup() {
  Diagnostics diagnostics;
  diagnostics.start(52, 300, 10000, 2000);
  diagnostics.observeProgress(52, 400, 11000, 3000, 3000);
  diagnostics.naturalEof(52, 401, 11000, 3000, 3000, true);
  diagnostics.transportError(52, 402, 11000, 3000, 3000, false);
  diagnostics.watchdogTimeout(52, 10402, 11000, 3000, 3000);
  diagnostics.cleanup(52, 10403, 11000, 3000, 3000);
  diagnostics.cleanup(52, 10404, 11000, 3000, 3000);

  const auto snapshot = diagnostics.snapshot();
  assert(snapshot.state == RunState::Cleaned);
  assert(snapshot.endReason == EndReason::NaturalEof);
  assert(snapshot.events.naturalEofs == 1);
  assert(snapshot.events.transportErrors == 1);
  assert(snapshot.events.watchdogTimeouts == 1);
  assert(snapshot.events.terminals == 1);
  assert(snapshot.events.cleanups == 1);
  assert(snapshot.events.ignoredTerminalEvents == 2);
  assert(snapshot.totals.naturalEofs == 1);
  assert(snapshot.totals.watchdogTimeouts == 0);
  assert(snapshot.totals.cleanups == 1);
}

static void testRunIdentityAndReplacement() {
  Diagnostics diagnostics;
  diagnostics.start(61, 100, 100, 50);
  diagnostics.observeProgress(61, 101, 200, 100, 400);
  diagnostics.replaced(61, 102, 250, 150, 400);
  diagnostics.start(62, 103, 250, 150);

  auto snapshot = diagnostics.snapshot();
  assert(snapshot.runId == 62);
  assert(snapshot.state == RunState::Active);
  assert(snapshot.endReason == EndReason::None);
  assert(snapshot.bytesRead == 0);
  assert(snapshot.events.starts == 1);
  assert(snapshot.events.replacements == 0);
  assert(snapshot.totals.runs == 2);
  assert(snapshot.totals.replacements == 1);

  diagnostics.transportError(61, 104, 300, 150, 0, true);
  snapshot = diagnostics.snapshot();
  assert(snapshot.runId == 62);
  assert(snapshot.endReason == EndReason::None);

  diagnostics.connectFailed(62, 105, 300, 150, 0, true);
  diagnostics.cleanup(62, 106, 300, 150, 0);
  snapshot = diagnostics.snapshot();
  assert(snapshot.endReason == EndReason::ConnectFailed);
  assert(snapshot.events.connectFailures == 1);
  assert(snapshot.events.cleanups == 1);
}

static void testCrossTaskNetworkNotification() {
  Diagnostics diagnostics;
  diagnostics.start(71, 1000, 9000, 1000);
  diagnostics.notifyNetworkInterrupted(70, true);
  assert(!diagnostics.consumeNetworkInterrupted(1001, 9100, 1100, 500));
  diagnostics.notifyNetworkInterrupted(71, false);
  assert(!diagnostics.consumeNetworkInterrupted(1002, 9200, 1200, 500));
  diagnostics.notifyNetworkInterrupted(71, true);
  assert(diagnostics.consumeNetworkInterrupted(1003, 9300, 1300, 500));
  diagnostics.cleanup(71, 1004, 9300, 1300, 500);

  const auto snapshot = diagnostics.snapshot();
  assert(snapshot.endReason == EndReason::NetworkInterrupted);
  assert(snapshot.events.networkInterruptions == 1);
  assert(snapshot.events.terminals == 1);
  assert(snapshot.events.cleanups == 1);
  assert(snapshot.bytesRead == 300);
}

static void testBackendStopFallback() {
  Diagnostics diagnostics;
  diagnostics.start(81, 400, 10000, 2000);
  diagnostics.cleanup(81, 450, 10123, 2123, 500);
  const auto snapshot = diagnostics.snapshot();
  assert(snapshot.state == RunState::Cleaned);
  assert(snapshot.endReason == EndReason::BackendStopped);
  assert(snapshot.events.backendStops == 1);
  assert(snapshot.events.terminals == 1);
  assert(snapshot.events.cleanups == 1);
}

static void testLateNetworkCauseCorrectsOnlyFallback() {
  Diagnostics diagnostics;
  diagnostics.start(82, 500, 20000, 3000);
  diagnostics.cleanup(82, 550, 20123, 3123, 500);
  diagnostics.notifyNetworkInterrupted(82, true);
  assert(diagnostics.consumeNetworkInterrupted(551, 20123, 3123, 0));

  const auto snapshot = diagnostics.snapshot();
  assert(snapshot.state == RunState::Cleaned);
  assert(snapshot.endReason == EndReason::NetworkInterrupted);
  assert(snapshot.events.backendStops == 0);
  assert(snapshot.events.networkInterruptions == 1);
  assert(snapshot.events.terminals == 1);
  assert(snapshot.events.cleanups == 1);
  assert(snapshot.events.ignoredTerminalEvents == 0);
  assert(snapshot.terminalAtMs == 550);
  assert(snapshot.cleanupAtMs == 550);
}

static void testConcurrentSnapshotCoherence() {
  Diagnostics diagnostics;
  diagnostics.start(91, 0, 100000, 200000);
  std::atomic<bool> complete{false};

  std::thread writer([&]() {
    for (uint32_t value = 1; value <= 50000; ++value) {
      diagnostics.observeProgress(91, value, 100000ULL + value,
                                  200000ULL + value, 50000);
    }
    diagnostics.naturalEof(91, 50001, 150000, 250000, 50000, true);
    diagnostics.cleanup(91, 50002, 150000, 250000, 50000);
    complete.store(true, std::memory_order_release);
  });

  while (!complete.load(std::memory_order_acquire)) {
    const auto snapshot = diagnostics.snapshot();
    assert(snapshot.runId == 91);
    assert(snapshot.bytesRead <= 50000);
    if (snapshot.bytesRead > 0 && snapshot.state == RunState::Active) {
      assert(snapshot.lastProgressAtMs == snapshot.bytesRead);
    }
  }
  writer.join();

  const auto snapshot = diagnostics.snapshot();
  assert(snapshot.state == RunState::Cleaned);
  assert(snapshot.endReason == EndReason::NaturalEof);
  assert(snapshot.bytesRead == 50000);
  assert(snapshot.events.progress == 50000);
  assert(snapshot.events.cleanups == 1);
}

int main() {
  std::atomic<uint32_t> atomicWord{0};
  assert(atomicWord.is_lock_free());
  assert(sizeof(Diagnostics) <= 192);
  testCompileFlagAndAuthenticatedRouteContract();
  testNaturalEofAndCleanup();
  testFirstTerminalReasonWins();
  testNaturalEofWinsLateDisconnectAndCleanup();
  testRunIdentityAndReplacement();
  testCrossTaskNetworkNotification();
  testBackendStopFallback();
  testLateNetworkCauseCorrectsOnlyFallback();
  testConcurrentSnapshotCoherence();
  std::cout << "tts_hil_diagnostics_native: PASS; storage="
            << sizeof(Diagnostics) << " bytes\n";
  return 0;
}
