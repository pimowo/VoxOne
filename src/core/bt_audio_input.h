#ifndef VOXONE_BT_AUDIO_INPUT_H
#define VOXONE_BT_AUDIO_INPUT_H

#include "options.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE && VOXONE_BT_I2S_RX_ENABLED

#include <atomic>
#include <stdint.h>
#include <freertos/FreeRTOS.h>

#include "bt_link_protocol.h"

class BtAudioInput {
 public:
  void begin();
  void loop(const BtLinkState& link, uint32_t nowMs);

 private:
  struct Stats {
    uint64_t bytesReceived = 0;
    uint32_t blocksReceived = 0;
    uint32_t readTimeouts = 0;
    uint32_t readErrors = 0;
    uint32_t lastDataMs = 0;
    uint16_t peak16 = 0;
  };

  static void readTask(void* context);
  void readContinuously();
  bool start(uint32_t rate, uint32_t nowMs);
  bool stop();

  bool active_ = false;
  bool retryPending_ = false;
  bool cleanupErrorReported_ = false;
  uint32_t sampleRate_ = 0;
  uint32_t lastFailureMs_ = 0;
  uint32_t lastReportMs_ = 0;
  uint64_t lastReportBytes_ = 0;
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> taskRunning_{false};
  std::atomic<int> readError_{0};
  portMUX_TYPE statsMux_ = portMUX_INITIALIZER_UNLOCKED;
  Stats stats_{};
};

extern BtAudioInput btAudioInput;

#endif
#endif
