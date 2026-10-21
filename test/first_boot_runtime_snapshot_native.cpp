#include <atomic>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include "../src/core/first_boot_runtime_snapshot.h"

namespace {
using voxone::first_boot::ApSecurity;
using voxone::first_boot::Connection;
using voxone::first_boot::Mode;
using voxone::first_boot::RuntimeConnectionTimeout;
using voxone::first_boot::RuntimeInvalidIpv4Readiness;
using voxone::first_boot::RuntimeSnapshot;
using voxone::first_boot::RuntimeSnapshotStore;
using voxone::first_boot::RuntimeWifiInventoryInvalid;
using voxone::first_boot::Screen;

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool contains(const char* text, const char* value) {
  return std::string(text).find(value) != std::string::npos;
}

bool contains(const RuntimeSnapshot& snapshot, const char* value) {
  return contains(snapshot.apSsid, value) ||
      contains(snapshot.apAddress, value) || contains(snapshot.title, value) ||
      contains(snapshot.statusText, value);
}

void assertTerminated(const RuntimeSnapshot& snapshot) {
  assert(snapshot.product[sizeof(snapshot.product) - 1] == '\0');
  assert(snapshot.apSsid[sizeof(snapshot.apSsid) - 1] == '\0');
  assert(snapshot.apAddress[sizeof(snapshot.apAddress) - 1] == '\0');
  assert(snapshot.title[sizeof(snapshot.title) - 1] == '\0');
  assert(snapshot.statusText[sizeof(snapshot.statusText) - 1] == '\0');
}

void assertCoherent(const RuntimeSnapshot& snapshot) {
  assertTerminated(snapshot);
  if (snapshot.connection == Connection::Connected)
    assert(snapshot.staValidatedInThisBoot);
  if (snapshot.apActive) {
    assert(snapshot.connection == Connection::SetupAp ||
           snapshot.connection == Connection::RecoveryAp);
    assert(snapshot.apSecurity != ApSecurity::NotActive);
    assert(snapshot.apSsid[0] != '\0');
    assert(snapshot.apAddress[0] != '\0');
  } else {
    assert(snapshot.apSecurity == ApSecurity::NotActive);
    assert(snapshot.apSsid[0] == '\0');
    assert(snapshot.apAddress[0] == '\0');
  }
  if (snapshot.presentApCredential) {
    assert(snapshot.mode == Mode::WifiRecovery);
    assert(snapshot.apSecurity == ApSecurity::Protected);
  }
}

void testLifecycle() {
  RuntimeSnapshotStore runtime;
  auto snapshot = runtime.read();
  assert(!snapshot.initialized && snapshot.generation == 0);
  assert(snapshot.mode == Mode::Normal && snapshot.screen == Screen::Player);
  assert(snapshot.connection == Connection::Idle && !snapshot.apActive);
  assert(!snapshot.staValidatedInThisBoot);
  assertCoherent(snapshot);

  runtime.initialize(0);
  snapshot = runtime.read();
  assert(snapshot.initialized && snapshot.generation == 1);
  assert(snapshot.mode == Mode::FirstBootSetup);
  assert(snapshot.screen == Screen::WifiSetup);
  assert(snapshot.connection == Connection::Idle && !snapshot.apActive);

  runtime.activateAp("VoxOne-ABC123", "192.168.4.1", true);
  snapshot = runtime.read();
  assert(snapshot.mode == Mode::FirstBootSetup);
  assert(snapshot.connection == Connection::SetupAp && snapshot.apActive);
  assert(snapshot.apSecurity == ApSecurity::Open);
  assert(!snapshot.presentApCredential);
  assert(std::string(snapshot.apSsid) == "VoxOne-ABC123");
  assert(std::string(snapshot.apAddress) == "192.168.4.1");
  assertCoherent(snapshot);

  RuntimeSnapshotStore sta;
  sta.initialize(1);
  snapshot = sta.read();
  assert(snapshot.mode == Mode::Normal && snapshot.connection == Connection::Idle);

  sta.beginStaAttempt();
  snapshot = sta.read();
  assert(snapshot.mode == Mode::WifiConnecting);
  assert(snapshot.screen == Screen::WifiProgress);
  assert(snapshot.connection == Connection::Connecting);
  assert(!snapshot.staValidatedInThisBoot);
  assert(!snapshot.hasError);

  sta.recordDisconnect(15);
  snapshot = sta.read();
  assert(snapshot.mode == Mode::WifiConnecting && snapshot.hasError);
  assert(snapshot.errorCode == 15);

  // The timeout belongs to the same attempt and must not hide a more useful
  // native WPA reason already observed for that attempt.
  sta.recordTimeout();
  assert(sta.read().errorCode == 15);

  // A new attempt cannot inherit the preceding attempt's reason.
  sta.beginStaAttempt();
  snapshot = sta.read();
  assert(!snapshot.hasError && snapshot.errorCode == 0);
  sta.recordTimeout();
  snapshot = sta.read();
  assert(snapshot.hasError && snapshot.errorCode == RuntimeConnectionTimeout);

  sta.activateAp("VoxOne-ABC123", "192.168.4.1", true);
  snapshot = sta.read();
  assert(snapshot.mode == Mode::WifiRecovery);
  assert(snapshot.screen == Screen::WifiRecovery);
  assert(snapshot.connection == Connection::RecoveryAp && snapshot.apActive);
  assert(snapshot.apSecurity == ApSecurity::Protected);
  assert(snapshot.presentApCredential);
  assert(snapshot.errorCode == RuntimeConnectionTimeout);
  assertCoherent(snapshot);

  // Late STA events from the failed attempt cannot overwrite active AP state.
  const uint32_t recoveryGeneration = snapshot.generation;
  sta.connected();
  sta.recordDisconnect(15);
  snapshot = sta.read();
  assert(snapshot.generation == recoveryGeneration);
  assert(snapshot.connection == Connection::RecoveryAp);

  sta.beginStaAttempt();
  sta.connected();
  snapshot = sta.read();
  assert(snapshot.mode == Mode::Normal && snapshot.screen == Screen::Player);
  assert(snapshot.connection == Connection::Connected);
  assert(snapshot.staValidatedInThisBoot);
  assert(!snapshot.hasError && !snapshot.apActive);
  assert(snapshot.mode != Mode::WifiConfigured);

  // Runtime loss and reassociation expose CONNECTING until validated GOT_IP.
  sta.recordDisconnect(15);
  snapshot = sta.read();
  assert(snapshot.errorCode == 15 && snapshot.staValidatedInThisBoot);
  sta.beginStaAttempt();
  snapshot = sta.read();
  assert(snapshot.mode == Mode::WifiConnecting && snapshot.errorCode == 0);
  assert(snapshot.staValidatedInThisBoot);
  sta.recordDisconnect(RuntimeInvalidIpv4Readiness);
  assert(sta.read().errorCode == RuntimeInvalidIpv4Readiness);
  sta.beginStaAttempt();
  sta.connected();
  snapshot = sta.read();
  assert(snapshot.mode == Mode::Normal && snapshot.staValidatedInThisBoot);

  // A new Network lifecycle, unlike a runtime reassociation, clears the
  // current-boot validation history.
  sta.initialize(1);
  snapshot = sta.read();
  assert(!snapshot.staValidatedInThisBoot);
}

void testInitializationFailureAndSecrets() {
  constexpr char secret[] = "never-publish-password-or-pin";
  RuntimeSnapshotStore runtime;
  runtime.inventoryInvalid();
  const RuntimeSnapshot failed = runtime.read();
  assert(failed.initialized && failed.connection == Connection::Failed);
  assert(failed.hasError &&
         failed.errorCode == RuntimeWifiInventoryInvalid);
  assert(!failed.apActive && !failed.presentApCredential);
  assert(!contains(failed, secret));

  runtime.initialize(0);
  runtime.activateAp(nullptr, nullptr, true);
  const RuntimeSnapshot incompleteAp = runtime.read();
  assert(incompleteAp.connection == Connection::Failed);
  assert(incompleteAp.hasError && !incompleteAp.apActive);
  assertCoherent(incompleteAp);

  runtime.initialize(1);
  runtime.beginStaAttempt();
  runtime.recordDisconnect(15);
  runtime.activateAp("VoxOne-SAFE01", "192.168.4.1", true);
  const RuntimeSnapshot recovery = runtime.read();
  assert(!contains(recovery, secret));

  const std::string header =
      readFile("src/core/first_boot_runtime_snapshot.h");
  assert(header.find("char password") == std::string::npos);
  assert(header.find("char pin") == std::string::npos);
  assert(header.find("char token") == std::string::npos);
}

void testConcurrentPublication() {
  RuntimeSnapshotStore runtime;
  runtime.initialize(1);
  std::atomic<bool> start{false};
  std::atomic<bool> done{false};
  std::atomic<bool> valid{true};

  std::thread writer([&]() {
    while (!start.load(std::memory_order_acquire)) {}
    for (uint32_t iteration = 0; iteration < 50000; ++iteration) {
      if ((iteration & 1U) == 0) {
        runtime.activateAp("VoxOne-AAAAAA", "10.0.0.1", true);
      } else {
        runtime.activateAp("VoxOne-BBBBBB", "10.0.0.2", true);
      }
      runtime.beginStaAttempt();
      if ((iteration & 3U) == 3U) runtime.connected();
    }
    done.store(true, std::memory_order_release);
  });

  std::thread reader([&]() {
    start.store(true, std::memory_order_release);
    do {
      const RuntimeSnapshot snapshot = runtime.read();
      assertCoherent(snapshot);
      if (snapshot.apActive) {
        const bool first = std::strcmp(snapshot.apSsid, "VoxOne-AAAAAA") == 0 &&
            std::strcmp(snapshot.apAddress, "10.0.0.1") == 0;
        const bool second = std::strcmp(snapshot.apSsid, "VoxOne-BBBBBB") == 0 &&
            std::strcmp(snapshot.apAddress, "10.0.0.2") == 0;
        if (!first && !second) valid.store(false, std::memory_order_release);
      } else if (snapshot.connection != Connection::Idle &&
                 snapshot.connection != Connection::Connecting &&
                 snapshot.connection != Connection::Connected) {
        valid.store(false, std::memory_order_release);
      } else if (snapshot.connection == Connection::Connected &&
                 !snapshot.staValidatedInThisBoot) {
        valid.store(false, std::memory_order_release);
      }
    } while (!done.load(std::memory_order_acquire));
  });

  writer.join();
  reader.join();
  assert(valid.load(std::memory_order_acquire));
}

void testProductionBindings() {
  const std::string network = readFile("src/core/network.cpp");
  const std::string networkHeader = readFile("src/core/network.h");
  assert(network.find("presentationState.initialize(config.ssidsCount)") !=
         std::string::npos);
  assert(network.find("presentationState.beginStaAttempt()") !=
         std::string::npos);
  assert(network.find("presentationState.recordDisconnect(") !=
         std::string::npos);
  assert(network.find("presentationState.recordTimeout()") !=
         std::string::npos);
  assert(network.find("presentationState.connected()") != std::string::npos);
  assert(network.find("presentationState.activateAp(") != std::string::npos);
  assert(networkHeader.find("presentationSnapshot() const") !=
         std::string::npos);
  assert(networkHeader.find("RuntimeSnapshotStore presentationState") !=
         std::string::npos);
}
}  // namespace

int main() {
#if !(defined(VOXONE_PROFILE_W0) || defined(VOXONE_PROFILE_M0) || \
      defined(VOXONE_PROFILE_L0) || defined(VOXONE_PROFILE_S0) || \
      defined(VOXONE_PROFILE_DIN0))
#error "FIRST-BOOT-RUNTIME-SNAPSHOT native test requires a VoxOne profile"
#endif
  testLifecycle();
  testInitializationFailureAndSecrets();
  testConcurrentPublication();
  testProductionBindings();
}
