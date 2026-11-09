#ifndef VOXONE_CONFIG_RUNTIME_SNAPSHOT_H
#define VOXONE_CONFIG_RUNTIME_SNAPSHOT_H

#include <atomic>
#include <stddef.h>
#include <stdint.h>

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#else
#include <mutex>
#endif

namespace voxone {
namespace config_runtime {

constexpr size_t kMdnsNameCapacity = 24;
constexpr size_t kHaNameCapacity = 33;

struct Snapshot {
  uint32_t revision;
  uint8_t volume;
  uint8_t userVolume;
  uint8_t lastUserVolume;
  uint8_t maximumVolume;
  uint8_t startupMode;
  uint8_t startupFixedVolume;
  int8_t balance;
  int8_t bass;
  int8_t middle;
  int8_t treble;
  uint16_t lastStation;
  uint16_t countStation;
  uint8_t lastSsid;
  bool intentionalNoStation;
  bool audioInfo;
  uint8_t smartStart;
  bool vuMeter;
  uint8_t softApDelay;
  bool flipScreen;
  bool invertDisplay;
  bool numPlaylist;
  bool dspOn;
  uint8_t brightness;
  uint8_t contrast;
  bool screensaverEnabled;
  uint16_t screensaverTimeout;
  bool screensaverBlank;
  bool screensaverPlayingEnabled;
  uint16_t screensaverPlayingTimeout;
  bool screensaverPlayingBlank;
  bool skipPlaylistUpDown;
  uint16_t audioBuffer;
  bool watchdog;
  uint16_t timeSyncInterval;
  uint16_t timeSyncIntervalRtc;
  int8_t timezoneHour;
  int8_t timezoneMinute;
  uint16_t encoderAcceleration;
  uint8_t volumeSteps;
  bool haDiscoveryEnabled;
  uint8_t tftTheme;
  uint8_t powerMode;
  bool encoderUiReversed;
  char mdnsName[kMdnsNameCapacity];
  char haName[kHaNameCapacity];
};

class SnapshotStore {
 public:
  SnapshotStore() : snapshot_{}, vuMeterEnabled_(false) {}
  SnapshotStore(const SnapshotStore&) = delete;
  SnapshotStore& operator=(const SnapshotStore&) = delete;

  Snapshot read() const {
    lock();
    const Snapshot value = snapshot_;
    unlock();
    return value;
  }

  void publish(Snapshot value) {
    lock();
    value.revision = snapshot_.revision + 1;
    snapshot_ = value;
    vuMeterEnabled_.store(value.vuMeter, std::memory_order_release);
    unlock();
  }

  bool audioVuMeterEnabled() const {
    return vuMeterEnabled_.load(std::memory_order_acquire);
  }

 private:
  void lock() const {
#if defined(ARDUINO)
    portENTER_CRITICAL(&mux_);
#else
    mutex_.lock();
#endif
  }

  void unlock() const {
#if defined(ARDUINO)
    portEXIT_CRITICAL(&mux_);
#else
    mutex_.unlock();
#endif
  }

  Snapshot snapshot_;
  std::atomic<bool> vuMeterEnabled_;
#if defined(ARDUINO)
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
#else
  mutable std::mutex mutex_;
#endif
};

class VuThresholdRuntime {
 public:
  void reset() { threshold_.store(0, std::memory_order_release); }

  void observe(uint16_t value) {
    uint32_t current = threshold_.load(std::memory_order_relaxed);
    while (value > current &&
           !threshold_.compare_exchange_weak(
               current, value, std::memory_order_release,
               std::memory_order_relaxed)) {}
  }

  void reduce(uint16_t value) {
    uint32_t current = threshold_.load(std::memory_order_relaxed);
    while (current != 0) {
      const uint32_t next = current > value ? current - value : 0;
      if (threshold_.compare_exchange_weak(
              current, next, std::memory_order_release,
              std::memory_order_relaxed))
        return;
    }
  }

  uint16_t read() const {
    return static_cast<uint16_t>(
        threshold_.load(std::memory_order_acquire));
  }

 private:
  std::atomic<uint32_t> threshold_{0};
};

static_assert(ATOMIC_BOOL_LOCK_FREE == 2,
              "Audio Config flags must be lock-free");
static_assert(ATOMIC_INT_LOCK_FREE == 2,
              "Audio VU threshold must be lock-free");

}  // namespace config_runtime
}  // namespace voxone

#endif
