#include "../src/core/ap_ui_www_binding.h"
#include "../src/core/network_ready_state.h"
#include "../src/core/wifi_config_atomic.h"
#include "../src/core/wifi_config_parser.h"
#include "../src/core/wifi_startup_retry.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace {

using voxone::first_boot::ApSecurity;
using voxone::first_boot::Connection;
using voxone::first_boot::RuntimeSnapshotStore;
using voxone::first_boot::WebPage;
using voxone::network::Ipv4ReadinessEvidence;
using voxone::network::NetworkReadyState;
using voxone::wifi::StartupFailureDecision;
using voxone::wifi::StartupRetryPolicy;
using namespace voxone::wifi_config_atomic;

struct FakeStorage {
  std::map<std::string, std::string> files;
  bool failWrite = false;
  unsigned writeCalls = 0;

  bool exists(const char* path) const { return files.count(path) != 0; }
  bool remove(const char* path) { return files.erase(path) == 1; }
  bool rename(const char* from, const char* to) {
    if (!exists(from) || exists(to)) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
  bool write(const char* path, const uint8_t* bytes, std::size_t size) {
    ++writeCalls;
    if (failWrite) return false;
    files[path] = std::string(reinterpret_cast<const char*>(bytes), size);
    return true;
  }
  bool matches(const char* path, const uint8_t* bytes,
               std::size_t size) const {
    const auto found = files.find(path);
    return found != files.end() && found->second.size() == size &&
        std::memcmp(found->second.data(), bytes, size) == 0;
  }
  bool validBytes(const uint8_t* bytes, std::size_t size) const {
    return voxone::wifi_config::validSerializedConfig(bytes, size);
  }
  bool valid(const char* path) const {
    const auto found = files.find(path);
    return found != files.end() && validBytes(
        reinterpret_cast<const uint8_t*>(found->second.data()),
        found->second.size());
  }
};

bool replaceText(FakeStorage& storage, const std::string& text) {
  return replace(storage, reinterpret_cast<const uint8_t*>(text.data()),
                 text.size());
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

Ipv4ReadinessEvidence validGotIp() {
  return {true, 0x0101A8C0U, 0x0101A8C0U, true, true, true,
          0x0101A8C0U};
}

void testFirstBootSinglePostToPlayer() {
  RuntimeSnapshotStore firstBoot;
  firstBoot.initialize(0);
  firstBoot.activateAp("VoxOne-SETUP1", "192.168.4.1", true);
  auto binding = voxone::first_boot::webBindingFor(firstBoot.read());
  assert(binding.page == WebPage::FirstBootSetup);

  FakeStorage storage;
  const std::string saved = "Home\tsecret\n";
  unsigned restartCount = 0;
  assert(replaceText(storage, saved));
  ++restartCount;
  assert(restartCount == 1 && storage.writeCalls == 1);

  voxone::wifi_config::Profile profiles[voxone::wifi_config::kMaxProfiles]{};
  const auto parsed = voxone::wifi_config::parseFile(
      reinterpret_cast<const uint8_t*>(storage.files[kActivePath].data()),
      storage.files[kActivePath].size(), profiles,
      voxone::wifi_config::kMaxProfiles);
  assert(parsed.valid() && parsed.profileCount == 1);

  RuntimeSnapshotStore restarted;
  restarted.initialize(static_cast<uint8_t>(parsed.profileCount));
  restarted.beginStaAttempt();
  StartupRetryPolicy retry;
  retry.begin(1, 0, 0);
  const auto attempt = retry.start(0);
  assert(attempt.valid && attempt.ordinal == 1 && attempt.profileIndex == 0);

  // Driver connection is not success; only the delayed validated GOT_IP is.
  NetworkReadyState readiness;
  constexpr bool driverReportsConnected = true;
  assert(driverReportsConnected && !readiness.ready());
  assert(!retry.timedOut(8999));
  assert(readiness.gotIp(validGotIp()));
  restarted.connected();
  assert(retry.succeed(attempt.generation, attempt.profileIndex));
  binding = voxone::first_boot::webBindingFor(restarted.read());
  assert(binding.page == WebPage::NormalPanel);
  assert(binding.normalPanel && !binding.wifiProvisioning);
  assert(restartCount == 1 && storage.files[kActivePath] == saved);
}

void testTransientFailuresRetryWithoutWrites() {
  FakeStorage storage;
  const std::string saved = "Home\tsecret\n";
  assert(replaceText(storage, saved));
  const unsigned writesAfterPost = storage.writeCalls;

  StartupRetryPolicy retry;
  retry.begin(1, 1, 0);
  auto first = retry.start(0);
  assert(first.valid && retry.timedOut(9000));
  assert(retry.fail(first.generation, first.profileIndex, 9000) ==
         StartupFailureDecision::Retry);
  assert(retry.dueAtMs() == 9500);
  assert(!retry.due(9499) && retry.due(9500));

  auto second = retry.start(9500);
  assert(second.ordinal == 2 && second.profileIndex == 0);
  assert(retry.fail(second.generation, second.profileIndex, 18500) ==
         StartupFailureDecision::Retry);
  assert(retry.dueAtMs() == 20000);

  auto third = retry.start(20000);
  NetworkReadyState readiness;
  assert(readiness.gotIp(validGotIp()));
  assert(retry.succeed(third.generation, third.profileIndex));
  assert(readiness.ready());
  assert(storage.writeCalls == writesAfterPost);
  assert(storage.files[kActivePath] == saved);
}

void testPermanentFailureEntersProtectedRecovery() {
  for (int failureKind = 0; failureKind < 3; ++failureKind) {
    StartupRetryPolicy retry;
    retry.begin(1, 1, 100);
    uint32_t now = 100;
    for (uint8_t expected = 1; expected <= 3; ++expected) {
      assert(retry.due(now));
      const auto attempt = retry.start(now);
      assert(attempt.ordinal == expected && attempt.total == 3);
      now += StartupRetryPolicy::kAttemptTimeoutMs;
      const auto decision = retry.fail(
          attempt.generation, attempt.profileIndex, now);
      if (expected < 3) {
        assert(decision == StartupFailureDecision::Retry);
        now = retry.dueAtMs();
      } else {
        assert(decision == StartupFailureDecision::Recovery);
      }
    }
    assert(!retry.hasRemaining());

    RuntimeSnapshotStore runtime;
    runtime.initialize(1);
    runtime.beginStaAttempt();
    if (failureKind == 0) runtime.recordDisconnect(202);  // bad credentials
    runtime.recordTimeout();  // unavailable AP or DHCP timeout
    runtime.activateAp("VoxOne-RECOV1", "192.168.4.1", true);
    const auto snapshot = runtime.read();
    const auto binding = voxone::first_boot::webBindingFor(snapshot);
    assert(binding.page == WebPage::WifiRecovery);
    assert(snapshot.connection == Connection::RecoveryAp);
    assert(snapshot.apSecurity == ApSecurity::Protected);
    assert(snapshot.presentApCredential);
  }
}

void testProfilesStaleEventsWrapAndRestart() {
  StartupRetryPolicy retry;
  retry.begin(5, 3, 0);
  const uint8_t expectedProfiles[] = {2, 3, 4, 0, 1, 2};
  uint32_t now = 0;
  uint32_t staleGeneration = 0;
  uint8_t staleProfile = 0;
  for (uint8_t index = 0; index < 6; ++index) {
    now = retry.dueAtMs();
    const auto attempt = retry.start(now);
    assert(attempt.profileIndex == expectedProfiles[index]);
    if (index == 0) {
      staleGeneration = attempt.generation;
      staleProfile = attempt.profileIndex;
    } else {
      assert(!retry.accepts(staleGeneration, staleProfile));
    }
    now += StartupRetryPolicy::kAttemptTimeoutMs;
    const auto decision = retry.fail(attempt.generation,
                                     attempt.profileIndex, now);
    assert(decision == (index == 5 ? StartupFailureDecision::Recovery
                                   : StartupFailureDecision::Retry));
  }

  StartupRetryPolicy wrapping;
  constexpr uint32_t start = UINT32_MAX - 200;
  wrapping.begin(1, 1, start);
  const auto wrappedAttempt = wrapping.start(start);
  assert(!wrapping.timedOut(start +
                           StartupRetryPolicy::kAttemptTimeoutMs - 1));
  const uint32_t wrappedTimeout =
      start + StartupRetryPolicy::kAttemptTimeoutMs;
  assert(wrapping.timedOut(wrappedTimeout));
  assert(wrapping.fail(wrappedAttempt.generation,
                       wrappedAttempt.profileIndex, wrappedTimeout) ==
         StartupFailureDecision::Retry);
  assert(!wrapping.due(wrapping.dueAtMs() - 1));
  assert(wrapping.due(wrapping.dueAtMs()));

  // A device restart creates a fresh bounded policy; it does not carry a
  // stale attempt identity across boots.
  StartupRetryPolicy afterRestart;
  afterRestart.begin(1, 1, 0);
  const auto fresh = afterRestart.start(0);
  assert(fresh.ordinal == 1 && fresh.generation == 1);
}

void testPersistenceFailureAndSourceContract() {
  FakeStorage storage;
  const std::string oldRecord = "Old\told-secret\n";
  storage.files[kActivePath] = oldRecord;
  storage.failWrite = true;
  assert(!replaceText(storage, "New\tnew-secret\n"));
  assert(storage.files[kActivePath] == oldRecord);

  const std::string network = readFile("src/core/network.cpp");
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string page = readFile("src/core/netserver.h");
  assert(network.find("while (!ready() && !retry.timedOut") !=
         std::string::npos);
  assert(network.find("while (WiFi.status() != WL_CONNECTED)") ==
         std::string::npos);
  assert(network.find("activeStaIdentityStillMatches") != std::string::npos);
  assert(network.find("recovery reason=attempts_exhausted") !=
         std::string::npos);
  assert(server.find("Saved Wi-Fi retry requested; restart scheduled") !=
         std::string::npos);
  assert(page.find("id=\"wifi-retry\"") != std::string::npos);
  assert(page.find("name=\"retry\"") != std::string::npos);
}

}  // namespace

int main() {
  testFirstBootSinglePostToPlayer();
  testTransientFailuresRetryWithoutWrites();
  testPermanentFailureEntersProtectedRecovery();
  testProfilesStaleEventsWrapAndRestart();
  testPersistenceFailureAndSourceContract();
}
