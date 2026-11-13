#include "../src/core/config_runtime_snapshot.h"
#include "../src/core/mute_state.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

using voxone::config_runtime::Snapshot;
using voxone::config_runtime::SnapshotStore;
using voxone::config_runtime::VuThresholdRuntime;

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string section(const std::string& source, const char* begin,
                    const char* end) {
  const std::size_t first = source.find(begin);
  assert(first != std::string::npos);
  const std::size_t last = source.find(end, first + 1);
  assert(last != std::string::npos && last > first);
  return source.substr(first, last - first);
}

Snapshot makeSnapshot(uint8_t id) {
  Snapshot value{};
  value.volume = static_cast<uint8_t>(10 + id);
  value.userVolume = static_cast<uint8_t>(20 + id);
  value.lastUserVolume = value.userVolume;
  value.maximumVolume = static_cast<uint8_t>(80 + id);
  value.startupMode = id & 1U;
  value.startupFixedVolume = static_cast<uint8_t>(30 + id);
  value.lineOutEnabled = (id & 1U) != 0;
  value.lineOutVolume = static_cast<uint8_t>(70 + id);
  value.balance = static_cast<int8_t>(id == 1 ? -6 : 6);
  value.bass = static_cast<int8_t>(id);
  value.middle = static_cast<int8_t>(id + 1);
  value.treble = static_cast<int8_t>(id + 2);
  value.lastStation = static_cast<uint16_t>(100 + id);
  value.countStation = static_cast<uint16_t>(200 + id);
  value.lastSsid = id;
  value.intentionalNoStation = id == 2;
  value.audioInfo = id == 1;
  value.smartStart = id;
  value.vuMeter = id == 2;
  value.softApDelay = static_cast<uint8_t>(3 + id);
  value.flipScreen = id == 1;
  value.invertDisplay = id == 2;
  value.numPlaylist = id == 1;
  value.dspOn = id == 2;
  value.brightness = static_cast<uint8_t>(40 + id);
  value.contrast = static_cast<uint8_t>(50 + id);
  value.screensaverEnabled = id == 1;
  value.screensaverTimeout = static_cast<uint16_t>(60 + id);
  value.screensaverBlank = id == 2;
  value.screensaverPlayingEnabled = id == 2;
  value.screensaverPlayingTimeout = static_cast<uint16_t>(70 + id);
  value.screensaverPlayingBlank = id == 1;
  value.skipPlaylistUpDown = id == 2;
  value.audioBuffer = static_cast<uint16_t>(7 + id);
  value.watchdog = id == 1;
  value.timeSyncInterval = static_cast<uint16_t>(80 + id);
  value.timeSyncIntervalRtc = static_cast<uint16_t>(90 + id);
  value.timezoneHour = static_cast<int8_t>(id);
  value.timezoneMinute = static_cast<int8_t>(id * 10);
  value.encoderAcceleration = static_cast<uint16_t>(100 + id);
  value.volumeSteps = id;
  value.tftTheme = id;
  value.powerMode = static_cast<uint8_t>(id - 1);
  value.encoderUiReversed = id == 2;
  std::snprintf(value.mdnsName, sizeof(value.mdnsName), "voxone-%u",
                static_cast<unsigned>(id));
  return value;
}

bool samePayload(const Snapshot& left, const Snapshot& right) {
  return left.volume == right.volume &&
      left.userVolume == right.userVolume &&
      left.lastUserVolume == right.lastUserVolume &&
      left.maximumVolume == right.maximumVolume &&
      left.startupMode == right.startupMode &&
      left.startupFixedVolume == right.startupFixedVolume &&
      left.lineOutEnabled == right.lineOutEnabled &&
      left.lineOutVolume == right.lineOutVolume &&
      left.balance == right.balance && left.bass == right.bass &&
      left.middle == right.middle && left.treble == right.treble &&
      left.lastStation == right.lastStation &&
      left.countStation == right.countStation &&
      left.lastSsid == right.lastSsid &&
      left.intentionalNoStation == right.intentionalNoStation &&
      left.audioInfo == right.audioInfo &&
      left.smartStart == right.smartStart &&
      left.vuMeter == right.vuMeter &&
      left.softApDelay == right.softApDelay &&
      left.flipScreen == right.flipScreen &&
      left.invertDisplay == right.invertDisplay &&
      left.numPlaylist == right.numPlaylist &&
      left.dspOn == right.dspOn &&
      left.brightness == right.brightness &&
      left.contrast == right.contrast &&
      left.screensaverEnabled == right.screensaverEnabled &&
      left.screensaverTimeout == right.screensaverTimeout &&
      left.screensaverBlank == right.screensaverBlank &&
      left.screensaverPlayingEnabled == right.screensaverPlayingEnabled &&
      left.screensaverPlayingTimeout == right.screensaverPlayingTimeout &&
      left.screensaverPlayingBlank == right.screensaverPlayingBlank &&
      left.skipPlaylistUpDown == right.skipPlaylistUpDown &&
      left.audioBuffer == right.audioBuffer &&
      left.watchdog == right.watchdog &&
      left.timeSyncInterval == right.timeSyncInterval &&
      left.timeSyncIntervalRtc == right.timeSyncIntervalRtc &&
      left.timezoneHour == right.timezoneHour &&
      left.timezoneMinute == right.timezoneMinute &&
      left.encoderAcceleration == right.encoderAcceleration &&
      left.volumeSteps == right.volumeSteps &&
      left.tftTheme == right.tftTheme &&
      left.powerMode == right.powerMode &&
      left.encoderUiReversed == right.encoderUiReversed &&
      std::strcmp(left.mdnsName, right.mdnsName) == 0;
}

void testCoherentConcurrentSnapshots() {
  SnapshotStore store;
  const Snapshot first = makeSnapshot(1);
  const Snapshot second = makeSnapshot(2);
  store.publish(first);
  assert(samePayload(store.read(), first));

  std::atomic<bool> start{false};
  std::atomic<bool> done{false};
  std::atomic<bool> valid{true};
  std::atomic<uint64_t> reads{0};

  std::thread writer([&]() {
    while (!start.load(std::memory_order_acquire)) {}
    for (uint32_t iteration = 0; iteration < 100000; ++iteration)
      store.publish((iteration & 1U) ? first : second);
    done.store(true, std::memory_order_release);
  });

  std::vector<std::thread> readers;
  for (unsigned reader = 0; reader < 5; ++reader) {
    readers.emplace_back([&]() {
      start.store(true, std::memory_order_release);
      do {
        const Snapshot current = store.read();
        if (!samePayload(current, first) && !samePayload(current, second))
          valid.store(false, std::memory_order_release);
        reads.fetch_add(1, std::memory_order_relaxed);
      } while (!done.load(std::memory_order_acquire));
    });
  }

  writer.join();
  for (std::thread& reader : readers) reader.join();
  assert(valid.load(std::memory_order_acquire));
  assert(reads.load(std::memory_order_acquire) > 0);
}

void testAudioAtomicsAndMute() {
  SnapshotStore store;
  Snapshot disabled = makeSnapshot(1);
  Snapshot enabled = makeSnapshot(2);
  disabled.vuMeter = false;
  enabled.vuMeter = true;
  store.publish(disabled);
  assert(!store.audioVuMeterEnabled());
  store.publish(enabled);
  assert(store.audioVuMeterEnabled());

  VuThresholdRuntime threshold;
  threshold.observe(100);
  threshold.observe(80);
  assert(threshold.read() == 100);
  threshold.reduce(10);
  assert(threshold.read() == 90);
  threshold.reduce(100);
  assert(threshold.read() == 0);

  std::vector<std::thread> producers;
  for (uint16_t value = 1; value <= 32; ++value)
    producers.emplace_back([&threshold, value]() { threshold.observe(value); });
  for (std::thread& producer : producers) producer.join();
  assert(threshold.read() == 32);

  MuteState mute;
  mute.setUserVolume(37);
  mute.set(true);
  MuteOutputSnapshot output = mute.outputSnapshot();
  assert(output.userVolume == 37 && output.muted && output.silent);
  mute.set(false);
  output = mute.outputSnapshot();
  assert(output.userVolume == 37 && !output.muted && !output.silent);
  assert(mute.stepUserVolume(output.userVolume, 1) == 38);
  output = mute.outputSnapshot();
  assert(output.userVolume == 38 && !output.muted && !output.silent);
}

void testProductionBindings() {
  const std::string configHeader = readFile("src/core/config.h");
  const std::string configSource = readFile("src/core/config.cpp");
  const std::string player = readFile("src/core/player.cpp");
  const std::string sourceManager = readFile("src/core/source_manager.cpp");
  const std::string netserver = readFile("src/core/netserver.cpp");
  const std::string display = readFile("src/core/display.cpp");
  const std::string dsp = readFile("src/core/dsp_transport_runtime.cpp");
  const std::string audio = readFile("src/audioI2S/Audio.cpp");
  const std::string main = readFile("src/main.cpp");
  const std::string web = readFile("web-src/voxone.js");
  const std::string runtime =
      readFile("src/core/config_runtime_snapshot.h");

  assert(configHeader.find("Snapshot runtimeSnapshot() const") !=
         std::string::npos);
  assert(configHeader.find("_publishRuntimeLocked();\n      if (commit") !=
         std::string::npos);
  assert(configSource.find("void Config::_publishRuntimeLocked()") !=
         std::string::npos);
  const std::string lineOutEnabled = section(
      configSource, "bool Config::setLineOutEnabled",
      "bool Config::setLineOutVolume");
  const std::string lineOutVolume = section(
      configSource, "bool Config::setLineOutVolume",
      "bool Config::setVuMeter");
  assert(lineOutEnabled.find("_pendingConfigSave.store") !=
         std::string::npos);
  assert(lineOutEnabled.find("persistV7") == std::string::npos);
  assert(lineOutVolume.find("_pendingConfigSave.store") !=
         std::string::npos);
  assert(lineOutVolume.find("persistV7") == std::string::npos);
  assert(configSource.find("void Config::servicePendingSave()") !=
         std::string::npos);
  assert(main.find("config.servicePendingSave();") != std::string::npos);
  assert(web.find("const maxShown = state.maximumVolume;") !=
         std::string::npos);
  assert(web.find("state.maximumVolume = maximum;") != std::string::npos);
  assert(web.find("volumeSettingEditing.maximumVolume") ==
         std::string::npos);
  assert(web.find("volumeSettingDragging.maximumVolume") ==
         std::string::npos);
  assert(runtime.find("EEPROM") == std::string::npos);
  assert(runtime.find("NVS") == std::string::npos);

  const std::string setUser = section(
      player, "void Player::setUserVol", "void Player::stepUserVol");
  const std::string stepUser = section(
      player, "void Player::stepUserVol", "void Player::applyCurrentVolume");
  for (const std::string* mutation : {&setUser, &stepUser}) {
    assert(mutation->find("Config::WriteLock") != std::string::npos);
    assert(mutation->find("_publishRuntimeLocked") != std::string::npos);
    assert(mutation->find("persistV7") == std::string::npos);
    assert(mutation->find("saveVolume") == std::string::npos);
  }

  assert(player.find("runtimeVolumeSnapshot() const") != std::string::npos);
  assert(sourceManager.find("player.outputSnapshot().userVolume") !=
         std::string::npos);
  assert(netserver.find("player.runtimeVolumeSnapshot()") !=
         std::string::npos);
  assert(netserver.find("const auto runtime = config.runtimeSnapshot();") !=
         std::string::npos);
  assert(display.find("const MuteOutputSnapshot output = player.outputSnapshot()") !=
         std::string::npos);
  assert(dsp.find("player.runtimeVolumeSnapshot()") != std::string::npos);

  const std::string computeVu = section(
      audio, "void Audio::_computeVUlevel", "uint16_t Audio::get_VUlevel");
  assert(computeVu.find("config.audioVuMeterEnabled()") !=
         std::string::npos);
  assert(computeVu.find("config.observeVuLevel") != std::string::npos);
  assert(computeVu.find("runtimeSnapshot") == std::string::npos);
  assert(computeVu.find("portENTER_CRITICAL") == std::string::npos);
}

}  // namespace

int main() {
  testCoherentConcurrentSnapshots();
  testAudioAtomicsAndMute();
  testProductionBindings();
  return 0;
}
