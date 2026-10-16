#include "../src/core/security_runtime.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

using namespace voxone::security;

namespace {

class FakeStorage : public SecurityStorage {
 public:
  SecurityStorageReadStatus read(uint8_t* output, size_t capacity,
                                 size_t& outputSize) override {
    ++reads;
    outputSize = 0;
    if (readError) return SecurityStorageReadStatus::Error;
    if (!present) return SecurityStorageReadStatus::Missing;
    outputSize = size;
    if (size <= capacity) std::memcpy(output, bytes, size);
    return SecurityStorageReadStatus::Found;
  }

  bool write(const uint8_t* input, size_t inputSize) override {
    if (inputSize > sizeof(bytes)) return false;
    std::memcpy(bytes, input, inputSize);
    size = inputSize;
    present = true;
    return true;
  }

  bool erase() override {
    present = false;
    size = 0;
    return true;
  }

  uint8_t bytes[kSecurityRecordSize]{};
  size_t size = 0;
  unsigned reads = 0;
  bool present = false;
  bool readError = false;
};

class DeterministicRandom : public SecurityRandomSource {
 public:
  bool fill(uint8_t* output, size_t size) override {
    for (size_t i = 0; i < size; ++i)
      output[i] = static_cast<uint8_t>(i + 1);
    return true;
  }
};

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void assertDiagnostics(const SecurityRuntimeDiagnostics& diagnostics,
                       SecurityState state, SecurityLoadStatus loadStatus,
                       bool wifiConfigured) {
  assert(diagnostics.state == state);
  assert(diagnostics.loadStatus == loadStatus);
  assert(diagnostics.wifiConfigured == wifiConfigured);
}

void testMissingRecordMigration() {
  DeterministicRandom random;
  FakeStorage firstBootStorage;
  SecurityService firstBoot(firstBootStorage, random);
  assertDiagnostics(initializeSecurityRuntime(firstBoot, false),
                    SecurityState::Unconfigured,
                    SecurityLoadStatus::Unconfigured, false);
  assert(firstBootStorage.reads == 1 && !firstBootStorage.present);

  FakeStorage legacyStorage;
  SecurityService legacy(legacyStorage, random);
  assertDiagnostics(initializeSecurityRuntime(legacy, true),
                    SecurityState::LegacyUnclaimed,
                    SecurityLoadStatus::LegacyUnclaimed, true);
  assert(legacyStorage.reads == 1 && !legacyStorage.present);
}

void testPersistentModes() {
  DeterministicRandom random;

  FakeStorage openStorage;
  SecurityService openWriter(openStorage, random);
  assert(openWriter.begin(true) == SecurityLoadStatus::LegacyUnclaimed);
  assert(openWriter.chooseOpen() == SecurityOperationStatus::Applied);
  SecurityService openReader(openStorage, random);
  assertDiagnostics(initializeSecurityRuntime(openReader, true),
                    SecurityState::Open, SecurityLoadStatus::Loaded, true);

  FakeStorage protectedStorage;
  SecurityService protectedWriter(protectedStorage, random);
  assert(protectedWriter.begin(true) ==
         SecurityLoadStatus::LegacyUnclaimed);
  assert(protectedWriter.chooseProtected("bezpieczne-haslo") ==
         SecurityOperationStatus::Applied);
  SecurityService protectedReader(protectedStorage, random);
  assertDiagnostics(initializeSecurityRuntime(protectedReader, true),
                    SecurityState::Protected, SecurityLoadStatus::Loaded,
                    true);
  assert(protectedReader.verifyPassword("bezpieczne-haslo"));
}

void testLockedDiagnostics() {
  DeterministicRandom random;

  FakeStorage corruptStorage;
  corruptStorage.present = true;
  corruptStorage.size = kSecurityRecordSize;
  std::memset(corruptStorage.bytes, 0xa5, sizeof(corruptStorage.bytes));
  SecurityService corrupt(corruptStorage, random);
  assertDiagnostics(initializeSecurityRuntime(corrupt, true),
                    SecurityState::Locked, SecurityLoadStatus::Corrupt, true);

  FakeStorage failedStorage;
  failedStorage.readError = true;
  SecurityService failed(failedStorage, random);
  assertDiagnostics(initializeSecurityRuntime(failed, true),
                    SecurityState::Locked,
                    SecurityLoadStatus::StorageError, true);
}

void testProductionStartupOrderAndIsolation() {
  const std::string mainSource = readFile("src/main.cpp");
  const std::string networkSource = readFile("src/core/network.cpp");
  const std::string serverSource = readFile("src/core/netserver.cpp");
  const std::string mqttSource = readFile("src/core/mqtt.cpp");

  const size_t configInit = mainSource.find("config.init();");
  const size_t networkBegin = mainSource.find("network.begin();");
  assert(configInit != std::string::npos && networkBegin != std::string::npos);
  assert(configInit < networkBegin);

  const size_t wifiInventory = networkSource.find("config.initNetwork()");
  const size_t securityInit =
      networkSource.find("initializeDeviceSecurity(");
  const size_t sessionInit =
      networkSource.find("deviceSecuritySessionService()");
  const size_t wifiStart = networkSource.find("setWifiParams();");
  const size_t mqttStart = networkSource.find("mqttInit();");
  assert(wifiInventory != std::string::npos &&
         securityInit != std::string::npos &&
         sessionInit != std::string::npos && wifiStart != std::string::npos &&
         mqttStart != std::string::npos);
  assert(wifiInventory < securityInit && securityInit < sessionInit &&
         sessionInit < wifiStart &&
         securityInit < mqttStart);

  assert(serverSource.find("security_runtime.h") == std::string::npos);
  assert(mqttSource.find("security_runtime.h") == std::string::npos);
  assert(networkSource.find("policy=backend-only") != std::string::npos);
}

}  // namespace

int main() {
  testMissingRecordMigration();
  testPersistentModes();
  testLockedDiagnostics();
  testProductionStartupOrderAndIsolation();
  return 0;
}
