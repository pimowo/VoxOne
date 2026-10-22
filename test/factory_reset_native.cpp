#include "../src/core/factory_reset.h"
#include "../src/core/security_recovery_key.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace voxone::factory_reset;
using voxone::security_recovery::BootKeyDetector;
using voxone::security_recovery::KeyState;

namespace {

class FakeBackend : public Backend {
 public:
  bool writeMarker(Marker value) override {
    if (markerWriteFails) return false;
    marker = value;
    return true;
  }

  Marker readMarker() override { return markerReadOverride ? readValue : marker; }

  bool clearMarker() override {
    if (markerClearFails) return false;
    marker = Marker::Missing;
    return true;
  }

  size_t nvsNamespaceCount() const override { return nvs.size(); }

  bool eraseNvsNamespace(size_t index) override {
    if (static_cast<int>(index) == failNvs) return false;
    nvs[index] = false;
    return true;
  }

  size_t userFileCount() const override { return files.size(); }

  bool eraseUserFile(size_t index) override {
    if (static_cast<int>(index) == failFile) return false;
    files[index] = false;
    return true;
  }

  bool userDataCleared() override {
    for (bool present : nvs)
      if (present) return false;
    for (bool present : files)
      if (present) return false;
    return factoryWwwPresent;
  }

  Marker marker = Marker::Missing;
  Marker readValue = Marker::Missing;
  std::vector<bool> nvs{true, true, true};
  std::vector<bool> files{true, true};
  int failNvs = -1;
  int failFile = -1;
  bool factoryWwwPresent = true;
  bool markerWriteFails = false;
  bool markerClearFails = false;
  bool markerReadOverride = false;
};

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

KeyState sampleLowKey() {
  BootKeyDetector detector;
  detector.begin(true, 0U);
  return detector.update(true, 50U);
}

void testSuccessfulResetAndOneShotFirstBootPriority() {
  FakeBackend backend;
  assert(perform(backend) == Result::Applied);
  assert(backend.marker == Marker::FirstBootPending);
  for (bool present : backend.nvs) assert(!present);
  for (bool present : backend.files) assert(!present);

  const bool factoryFirstBoot = consumeFirstBootBypass(backend);
  const KeyState firstBootKey = factoryFirstBoot ? KeyState::Cancelled
                                                 : sampleLowKey();
  assert(factoryFirstBoot);
  assert(firstBootKey == KeyState::Cancelled);
  assert(backend.marker == Marker::Missing);

  const bool nextFactoryFirstBoot = consumeFirstBootBypass(backend);
  const KeyState nextBootKey = nextFactoryFirstBoot ? KeyState::Cancelled
                                                    : sampleLowKey();
  assert(!nextFactoryFirstBoot);
  assert(nextBootKey == KeyState::Accepted);
}

void testFailuresNeverProduceFactorySuccess() {
  FakeBackend marker;
  marker.markerWriteFails = true;
  assert(perform(marker) == Result::MarkerWriteFailed);
  assert(marker.marker != Marker::FirstBootPending);

  FakeBackend nvs;
  nvs.failNvs = 1;
  assert(perform(nvs) == Result::NvsEraseFailed);
  assert(nvs.marker == Marker::Erasing);

  FakeBackend file;
  file.failFile = 0;
  assert(perform(file) == Result::FileEraseFailed);
  assert(file.marker == Marker::Erasing);

  FakeBackend assets;
  assets.factoryWwwPresent = false;
  assert(perform(assets) == Result::VerificationFailed);
  assert(assets.marker == Marker::Erasing);

  FakeBackend finalize;
  finalize.markerReadOverride = true;
  finalize.readValue = Marker::Erasing;
  assert(perform(finalize) == Result::FinalizeFailed);

  FakeBackend consume;
  consume.marker = Marker::FirstBootPending;
  consume.markerClearFails = true;
  assert(!consumeFirstBootBypass(consume));
  assert(consume.marker == Marker::FirstBootPending);
}

void testProductionResetInventoryAndPreservationBoundary() {
  const std::string runtime = readFile("src/core/factory_reset_runtime.cpp");
  for (const char* name : {"eeprom", "nvs.net80211", "voxhw", "voxmqtt",
                           "voxui", "voxscroll", "voxsecurity",
                           "voxrestore", "voxfirstboot", "voxupdate"})
    assert(runtime.find(std::string("\"") + name + "\"") !=
           std::string::npos);
  for (const char* path : {"/data/wifi.csv", "/data/stations.tsv",
                           "/data/stations.idx", "/data/playlist.csv",
                           "/data/stations.upload"})
    assert(runtime.find(path) != std::string::npos);
  assert(runtime.find("/www/assets.vxwm") != std::string::npos);
  assert(runtime.find("SPIFFS.format") == std::string::npos);
  assert(runtime.find("nvs_flash_erase") == std::string::npos);
  assert(runtime.find("esp_ota_") == std::string::npos);
  assert(runtime.find("\"vox_ota\"") == std::string::npos);
}

}  // namespace

int main() {
  testSuccessfulResetAndOneShotFirstBootPriority();
  testFailuresNeverProduceFactorySuccess();
  testProductionResetInventoryAndPreservationBoundary();
  return 0;
}
