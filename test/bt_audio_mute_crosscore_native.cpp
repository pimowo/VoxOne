#include "../src/core/mute_state.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace {

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string functionBody(const std::string& source,
                         const std::string& signature) {
  const size_t begin = source.find(signature);
  assert(begin != std::string::npos);
  const size_t open = source.find('{', begin);
  assert(open != std::string::npos);
  size_t depth = 0;
  for (size_t pos = open; pos < source.size(); ++pos) {
    if (source[pos] == '{') {
      ++depth;
    } else if (source[pos] == '}' && --depth == 0) {
      return source.substr(open, pos - open + 1);
    }
  }
  assert(false);
  return {};
}

void assertCriticalSectionContains(const std::string& source,
                                   const std::string& signature,
                                   const char* operation) {
  const std::string body = functionBody(source, signature);
  const size_t enter = body.find("portENTER_CRITICAL(&playerVolumeMux)");
  const size_t update = body.find(operation);
  const size_t exit = body.find("portEXIT_CRITICAL(&playerVolumeMux)", update);
  assert(enter != std::string::npos);
  assert(update != std::string::npos);
  assert(exit != std::string::npos);
  assert(enter < update && update < exit);
}

void testProductionUsesLockFreeSnapshot() {
  const std::string mute = readFile("src/core/mute_state.h");
  const std::string player = readFile("src/core/player.cpp");
  const std::string config = readFile("src/core/config.cpp");
  const std::string input = readFile("src/core/bt_audio_input.cpp");

  assert(mute.find("std::atomic<uint8_t> outputState_{0}") !=
         std::string::npos);
  assert(mute.find("ATOMIC_CHAR_LOCK_FREE == 2") != std::string::npos);
  assert(mute.find("outputState_.load(std::memory_order_acquire)") !=
         std::string::npos);

  const std::string outputSilent =
      functionBody(player, "bool Player::outputSilent() const");
  assert(outputSilent.find("_mute.outputSilent()") != std::string::npos);
  assert(outputSilent.find("config.userVolume") == std::string::npos);
  assert(outputSilent.find("portENTER_CRITICAL") == std::string::npos);
  assert(outputSilent.find("xSemaphoreTake") == std::string::npos);

  assert(player.find("_mute.setUserVolume(runtime.userVolume)") !=
         std::string::npos);
  assertCriticalSectionContains(
      player, "void Player::setMuted(", "_mute.set(muted)");
  assertCriticalSectionContains(
      player, "void Player::setUserVol(", "_mute.setUserVolume(user)");
  assertCriticalSectionContains(
      player, "void Player::stepUserVol(", "_mute.stepUserVolume(");
  assertCriticalSectionContains(
      config, "uint8_t Config::setVolumeState(",
      "player._syncOutputSilence(user)");

  const std::string readLoop =
      functionBody(input, "void BtAudioInput::readContinuously()");
  const size_t read = readLoop.find("i2s_read(");
  const size_t snapshot = readLoop.find("player.outputSilent()");
  const size_t write = readLoop.find("i2s_write(");
  assert(read != std::string::npos);
  assert(snapshot != std::string::npos);
  assert(write != std::string::npos);
  assert(read < snapshot && snapshot < write);
  assert(readLoop.find("playerVolumeMux") == std::string::npos);
}

void assertSnapshot(const MuteOutputSnapshot& snapshot, uint8_t volume,
                    bool muted, bool silent) {
  assert(snapshot.userVolume == volume);
  assert(snapshot.muted == muted);
  assert(snapshot.silent == silent);
}

void testVolumeAndMuteTransitions() {
  MuteState state;
  assertSnapshot(state.outputSnapshot(), 0, false, true);

  state.setUserVolume(37);
  assertSnapshot(state.outputSnapshot(), 37, false, false);

  state.setUserVolume(0);
  assertSnapshot(state.outputSnapshot(), 0, false, true);

  state.setUserVolume(62);
  assertSnapshot(state.outputSnapshot(), 62, false, false);
  state.set(true);
  assertSnapshot(state.outputSnapshot(), 62, true, true);
  state.set(false);
  assertSnapshot(state.outputSnapshot(), 62, false, false);

  state.set(true);
  assert(state.stepUserVolume(62, 1) == 63);
  assertSnapshot(state.outputSnapshot(), 63, false, false);
  assert(state.stepUserVolume(1, -1) == 0);
  assertSnapshot(state.outputSnapshot(), 0, false, true);

  state.setUserVolume(255);
  assertSnapshot(state.outputSnapshot(), 100, false, false);
}

void testConcurrentPublishAndAudioReadStayCoherent() {
  static_assert(ATOMIC_CHAR_LOCK_FREE == 2,
                "test requires a lock-free production snapshot");
  MuteState state;
  state.setUserVolume(50);
  std::atomic<bool> start{false};
  std::atomic<unsigned> writersDone{0};
  std::atomic<size_t> reads{0};
  constexpr size_t kRounds = 250000;

  std::thread volumeWriter([&] {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    for (size_t round = 0; round < kRounds; ++round) {
      state.setUserVolume(0);
      state.setUserVolume(100);
    }
    writersDone.fetch_add(1, std::memory_order_release);
  });

  std::thread muteWriter([&] {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    for (size_t round = 0; round < kRounds; ++round) {
      state.set(true);
      state.set(false);
    }
    writersDone.fetch_add(1, std::memory_order_release);
  });

  std::thread audioReader([&] {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    while (writersDone.load(std::memory_order_acquire) != 2) {
      const MuteOutputSnapshot snapshot = state.outputSnapshot();
      assert(snapshot.userVolume == 0 || snapshot.userVolume == 50 ||
             snapshot.userVolume == 100);
      assert(snapshot.silent ==
             (snapshot.muted || snapshot.userVolume == 0));
      ++reads;
    }
  });

  start.store(true, std::memory_order_release);
  volumeWriter.join();
  muteWriter.join();
  audioReader.join();
  assert(reads.load() > 0);

  const MuteOutputSnapshot final = state.outputSnapshot();
  assert(final.userVolume <= 100);
  assert(final.silent == (final.muted || final.userVolume == 0));
}

}  // namespace

int main() {
  testProductionUsesLockFreeSnapshot();
  testVolumeAndMuteTransitions();
  testConcurrentPublishAndAudioReadStayCoherent();
  std::puts("PASS bt_audio_mute_crosscore_native");
  return 0;
}
