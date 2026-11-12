#include "bt_audio_input.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE && VOXONE_BT_I2S_RX_ENABLED

#include <driver/i2s.h>
#include <freertos/task.h>

#include "bt_audio_input_state.h"
#include "bt_runtime.h"
#include "bt_audio_route_state.h"
#include "i2s_output_lock.h"
#include "player.h"
#include "serialcli.h"
#include "system_operation_state.h"
#include "../hardware/hardware_descriptor.h"

namespace {
constexpr i2s_port_t kBtI2sPort = I2S_NUM_1;
constexpr uint32_t kReportIntervalMs = 5000;
constexpr uint32_t kReadWaitMs = 20;
constexpr uint32_t kWriteWaitMs = 10;
constexpr uint8_t kWriteAttempts = 3;
constexpr uint32_t kRouteLockWaitMs = 30;
constexpr uint32_t kTaskStopWaitMs = 100;
constexpr uint32_t kTaskStackBytes = 4096;
constexpr UBaseType_t kTaskPriority = 2;
constexpr BaseType_t kTaskCore = 1;
}  // namespace

BtAudioInput btAudioInput;

void BtAudioInput::begin() {
  if (!voxone::i2s_output::begin())
    serialCli.printf("##[BT-AUDIO]# I2S0 route mutex creation failed\n");
  lastReportMs_ = millis();
  taskStopped_ = xSemaphoreCreateBinaryStatic(&taskStoppedStorage_);
  if (!taskStopped_)
    serialCli.printf("##[BT-AUDIO]# RX completion semaphore creation failed\n");
}

bool BtAudioInput::blockForUpdate() {
  if (!btRuntime.physicalStarted()) return true;
  routeEnabled_.store(false);
  radioReady_.store(false);
  voxone::i2s_output::Guard outputGuard(pdMS_TO_TICKS(100));
  if (!outputGuard) return false;
  const esp_err_t error = i2s_zero_dma_buffer(I2S_NUM_0);
  return error == ESP_OK;
}

bool BtAudioInput::acquirePlayerOutput() {
  if (!btRuntime.physicalStarted()) return true;
  routeEnabled_.store(false);
  radioReady_.store(false);
  // Drain any in-flight BT write before Player resets its decoder/I2S clock.
  voxone::i2s_output::Guard outputGuard(pdMS_TO_TICKS(100));
  if (!outputGuard) return false;
  const esp_err_t error = i2s_zero_dma_buffer(I2S_NUM_0);
  if (error == ESP_OK) {
    bluetoothSelected_ = false;
    outputRate_ = player.getSampleRate();
    radioReady_.store(true);
  }
  return error == ESP_OK;
}

void BtAudioInput::route(bool bluetoothSelected, bool physicalStreamStarted,
                         uint32_t radioRate) {
  if (systemUpdateAudioBlocked()) {
    routeEnabled_.store(false);
    radioReady_.store(false);
    return;
  }
  // While RADIO owns I2S0, Audio::setSampleRate() already manages its clock.
  if (!bluetoothSelected && radioRate != 0 && !bluetoothSelected_ &&
      radioReady_.load()) {
    outputRate_ = radioRate;
    return;
  }
  const BtAudioRouteTarget target = btAudioRouteTarget(
      bluetoothSelected, physicalStreamStarted,
      active_ && taskRunning_.load() &&
          !stopRequested_.load(), sampleRate_, radioRate);
  const uint32_t targetRate = target.outputRate;
  const bool desiredBt = target.btOutput;
  if (bluetoothSelected == bluetoothSelected_ &&
      outputRate_ == targetRate && routeEnabled_.load() == desiredBt &&
      radioReady_.load() == target.radioOutput)
    return;

  routeEnabled_.store(false);
  radioReady_.store(false);
  voxone::i2s_output::Guard outputGuard(
      pdMS_TO_TICKS(kRouteLockWaitMs));
  if (!outputGuard) return;

  const bool changedSource = bluetoothSelected != bluetoothSelected_;
  const bool changedRate = targetRate != outputRate_;
  esp_err_t error = ESP_OK;
  if (changedSource || changedRate) error = i2s_zero_dma_buffer(I2S_NUM_0);
  if (error == ESP_OK && targetRate != 0 && (changedSource || changedRate))
    error = i2s_set_sample_rates(I2S_NUM_0, targetRate);
  if (error == ESP_OK && target.btOutput && (changedSource || changedRate))
    error = i2s_start(I2S_NUM_0);
  if (error == ESP_OK) {
    bluetoothSelected_ = bluetoothSelected;
    outputRate_ = targetRate;
    routeEnabled_.store(target.btOutput);
    radioReady_.store(target.radioOutput);
    if (bluetoothSelected || changedSource)
      player.setOutputPins(target.btOutput);
  }
  if (error != ESP_OK) {
    if (!routeErrorReported_)
      serialCli.printf("##[BT-AUDIO]# I2S0 route failed err=%d\n", error);
    routeErrorReported_ = true;
  } else if (changedSource || changedRate) {
    routeErrorReported_ = false;
    serialCli.printf("##[BT-AUDIO]# route=%s rate=%lu\n",
                     bluetoothSelected ? "BT" : "RADIO",
                     static_cast<unsigned long>(targetRate));
  } else {
    routeErrorReported_ = false;
  }
}

bool BtAudioInput::start(uint32_t rate, uint32_t nowMs) {
  if (!taskStopped_) return false;
  i2s_config_t config{};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_SLAVE | I2S_MODE_RX);
  config.sample_rate = rate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = false;

  esp_err_t error = i2s_driver_install(kBtI2sPort, &config, 0, nullptr);
  if (error != ESP_OK) {
    serialCli.printf("##[BT-AUDIO]# I2S1 install failed err=%d\n", error);
    return false;
  }

  const auto& btPins = voxone::hardware::currentHardware().btAudioIn;
  i2s_pin_config_t pins{};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = btPins.bclk;
  pins.ws_io_num = btPins.ws;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = btPins.din;
  error = i2s_set_pin(kBtI2sPort, &pins);
  if (error != ESP_OK) {
    i2s_driver_uninstall(kBtI2sPort);
    serialCli.printf("##[BT-AUDIO]# I2S1 pins failed err=%d\n", error);
    return false;
  }

  stopRequested_.store(false);
  clearPhysicalStream();
  readError_.store(0);
  sampleRate_ = rate;
  lastReportMs_ = nowMs;
  lastReportBytes_ = 0;
  lastReportTxBytes_ = 0;
  portENTER_CRITICAL(&statsMux_);
  stats_ = Stats{};
  portEXIT_CRITICAL(&statsMux_);
  taskRunning_.store(true);
  while (xSemaphoreTake(taskStopped_, 0) == pdTRUE) {}
  if (xTaskCreatePinnedToCore(readTask, "BtI2SRx", kTaskStackBytes, this,
                              kTaskPriority, nullptr, kTaskCore) != pdPASS) {
    taskRunning_.store(false);
    i2s_stop(kBtI2sPort);
    i2s_driver_uninstall(kBtI2sPort);
    serialCli.printf("##[BT-AUDIO]# I2S1 RX task creation failed\n");
    return false;
  }
  taskExitPending_ = true;

  active_ = true;
  serialCli.printf("##[BT-AUDIO]# I2S1 RX started rate=%lu BCLK=%d WS=%d DATA=%d\n",
                   static_cast<unsigned long>(rate), btPins.bclk,
                   btPins.ws, btPins.din);
  return true;
}

bool BtAudioInput::stop() {
  if (!active_) return true;
  stopRequested_.store(true);
  routeEnabled_.store(false);
  // Only the RX task calls i2s_read(). Its explicit completion handshake is
  // the ownership boundary: never stop/uninstall I2S1 while that call or the
  // task epilogue can still be running.
  if (taskExitPending_) {
    if (xSemaphoreTake(taskStopped_, pdMS_TO_TICKS(kTaskStopWaitMs)) != pdTRUE)
      return false;
    taskExitPending_ = false;
  }
  if (taskRunning_.load()) return false;
  i2s_stop(kBtI2sPort);
  const esp_err_t error = i2s_driver_uninstall(kBtI2sPort);
  if (error != ESP_OK) {
    if (!cleanupErrorReported_)
      serialCli.printf("##[BT-AUDIO]# I2S1 cleanup failed err=%d\n", error);
    cleanupErrorReported_ = true;
    return false;
  }
  cleanupErrorReported_ = false;
  active_ = false;
  sampleRate_ = 0;
  clearPhysicalStream();
  serialCli.printf("##[BT-AUDIO]# I2S1 RX stopped\n");
  return true;
}

void BtAudioInput::readTask(void* context) {
  static_cast<BtAudioInput*>(context)->readContinuously();
}

void BtAudioInput::readContinuously() {
  int16_t pcm[512];
  while (!stopRequested_.load()) {
    size_t bytesRead = 0;
    const esp_err_t error = i2s_read(kBtI2sPort, pcm, sizeof(pcm), &bytesRead,
                                     pdMS_TO_TICKS(kReadWaitMs));
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
      portENTER_CRITICAL(&statsMux_);
      ++stats_.readErrors;
      portEXIT_CRITICAL(&statsMux_);
      readError_.store(error);
      break;
    }
    if (bytesRead == 0) {
      portENTER_CRITICAL(&statsMux_);
      ++stats_.readTimeouts;
      portEXIT_CRITICAL(&statsMux_);
      continue;
    }
    uint16_t blockPeak = 0;
    for (size_t index = 0; index < bytesRead / sizeof(int16_t); ++index) {
      const int sample = static_cast<int>(pcm[index]);
      const uint16_t magnitude =
          static_cast<uint16_t>(sample < 0 ? -sample : sample);
      if (magnitude > blockPeak) blockPeak = magnitude;
    }
    const uint32_t dataMs = millis();
    lastPcmMs_.store(dataMs, std::memory_order_relaxed);
    pcmSeen_.store(true, std::memory_order_release);
    portENTER_CRITICAL(&statsMux_);
    stats_.bytesReceived += bytesRead;
    ++stats_.blocksReceived;
    stats_.lastDataMs = dataMs;
    if (blockPeak > stats_.peak16) stats_.peak16 = blockPeak;
    portEXIT_CRITICAL(&statsMux_);
    if (routeEnabled_.load() && !systemUpdateAudioBlocked()) {
      if (player.outputSilent()) memset(pcm, 0, bytesRead);
      size_t totalWritten = 0;
      uint32_t writeErrors = 0;
      uint32_t writeRetries = 0;
      for (uint8_t attempt = 0;
           attempt < kWriteAttempts && totalWritten < bytesRead &&
           routeEnabled_.load() && !systemUpdateAudioBlocked(); ++attempt) {
        if (attempt != 0) ++writeRetries;
        size_t bytesWritten = 0;
        esp_err_t writeError = ESP_ERR_TIMEOUT;
        {
          voxone::i2s_output::Guard outputGuard(
              pdMS_TO_TICKS(kWriteWaitMs));
          if (outputGuard && routeEnabled_.load() &&
              !systemUpdateAudioBlocked()) {
            writeError = i2s_write(
                I2S_NUM_0,
                reinterpret_cast<uint8_t*>(pcm) + totalWritten,
                bytesRead - totalWritten, &bytesWritten,
                pdMS_TO_TICKS(kWriteWaitMs));
          }
        }
        totalWritten += bytesWritten;
        if (writeError != ESP_OK) ++writeErrors;
      }
      portENTER_CRITICAL(&statsMux_);
      stats_.bytesTransmitted += totalWritten;
      if (totalWritten) ++stats_.blocksTransmitted;
      stats_.writeErrors += writeErrors;
      stats_.writeRetries += writeRetries;
      if (totalWritten != bytesRead) {
        ++stats_.shortWrites;
        stats_.droppedBytes += bytesRead - totalWritten;
      }
      portEXIT_CRITICAL(&statsMux_);
    } else if (routeEnabled_.load()) {
      portENTER_CRITICAL(&statsMux_);
      ++stats_.shortWrites;
      portEXIT_CRITICAL(&statsMux_);
    }
  }
  taskRunning_.store(false);
  xSemaphoreGive(taskStopped_);
  vTaskDelete(nullptr);
}

bool BtAudioInput::physicalStreamStarted(uint32_t nowMs) const {
  const bool seen = pcmSeen_.load(std::memory_order_acquire);
  const uint32_t lastPcmMs = lastPcmMs_.load(std::memory_order_relaxed);
  return btPcmStreamStarted(seen, lastPcmMs, nowMs);
}

void BtAudioInput::clearPhysicalStream() {
  pcmSeen_.store(false, std::memory_order_release);
  lastPcmMs_.store(0, std::memory_order_relaxed);
}

void BtAudioInput::recordRetryFailure(uint32_t nowMs) {
  retryState_.failed(nowMs);
  if (retryState_.exhausted() && !retryLimitReported_) {
    serialCli.printf("##[BT-AUDIO]# I2S1 retry limit reached attempts=%u\n",
                     static_cast<unsigned>(retryState_.attempts()));
    retryLimitReported_ = true;
  }
}

void BtAudioInput::loop(const BtLinkState& link, bool bluetoothSelected,
                        uint32_t radioRate, uint32_t nowMs) {
  const uint32_t desiredRate = btAudioDesiredRate(link);
  const bool streamStarted = desiredRate != 0 && physicalStreamStarted(nowMs);
  if (desiredRate == 0) {
    route(bluetoothSelected, false, radioRate);
    stop();
    retryState_.reset();
    retryLimitReported_ = false;
    clearPhysicalStream();
    return;
  }
  if (active_ && stopRequested_.load() && !stop()) {
    route(bluetoothSelected, streamStarted, radioRate);
    return;
  }
  if (active_ && sampleRate_ != desiredRate) {
    route(false, false, radioRate);
    if (!stop()) return;
    retryState_.reset();
    retryLimitReported_ = false;
  }
  if (active_ && !taskRunning_.load()) {
    const int error = readError_.load();
    if (!stop()) return;
    if (error != 0)
      serialCli.printf("##[BT-AUDIO]# I2S1 read failed err=%d\n", error);
    recordRetryFailure(nowMs);
  }
  if (!active_) {
    if (!retryState_.canAttempt(nowMs)) {
      route(bluetoothSelected, false, radioRate);
      return;
    }
    if (!start(desiredRate, nowMs)) {
      recordRetryFailure(nowMs);
      return;
    }
    retryState_.started();
  }

  const bool physicalStarted = physicalStreamStarted(nowMs);
  if (physicalStarted && retryState_.attempts() != 0) {
    retryState_.reset();
    retryLimitReported_ = false;
  }
  route(bluetoothSelected, physicalStarted, radioRate);

  if (active_ && nowMs - lastReportMs_ >= kReportIntervalMs) {
    Stats snapshot;
    portENTER_CRITICAL(&statsMux_);
    snapshot = stats_;
    stats_.peak16 = 0;
    portEXIT_CRITICAL(&statsMux_);
    const uint32_t elapsedMs = nowMs - lastReportMs_;
    const uint32_t bytesPerSecond = static_cast<uint32_t>(
        (snapshot.bytesReceived - lastReportBytes_) * 1000 / elapsedMs);
    const uint32_t txBytesPerSecond = static_cast<uint32_t>(
        (snapshot.bytesTransmitted - lastReportTxBytes_) * 1000 / elapsedMs);
    const uint32_t expectedBytesPerSecond = sampleRate_ * 4;
    const uint32_t percent = bytesPerSecond * 100 / expectedBytesPerSecond;
    serialCli.printf(
        "##[BT-AUDIO]# rate=%lu rx=%lu B/s tx=%lu B/s pct=%lu bytes=%llu txBytes=%llu rxBlocks=%lu txBlocks=%lu timeouts=%lu errors=%lu writeErrors=%lu writeRetries=%lu shortWrites=%lu droppedBytes=%llu peak=%u lastDataMs=%lu\n",
        static_cast<unsigned long>(sampleRate_),
        static_cast<unsigned long>(bytesPerSecond),
        static_cast<unsigned long>(txBytesPerSecond),
        static_cast<unsigned long>(percent),
        static_cast<unsigned long long>(snapshot.bytesReceived),
        static_cast<unsigned long long>(snapshot.bytesTransmitted),
        static_cast<unsigned long>(snapshot.blocksReceived),
        static_cast<unsigned long>(snapshot.blocksTransmitted),
        static_cast<unsigned long>(snapshot.readTimeouts),
        static_cast<unsigned long>(snapshot.readErrors),
        static_cast<unsigned long>(snapshot.writeErrors),
        static_cast<unsigned long>(snapshot.writeRetries),
        static_cast<unsigned long>(snapshot.shortWrites),
        static_cast<unsigned long long>(snapshot.droppedBytes),
        static_cast<unsigned>(snapshot.peak16),
        static_cast<unsigned long>(snapshot.lastDataMs));
    lastReportBytes_ = snapshot.bytesReceived;
    lastReportTxBytes_ = snapshot.bytesTransmitted;
    lastReportMs_ = nowMs;
  }
}

#endif
