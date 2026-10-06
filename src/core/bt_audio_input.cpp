#include "bt_audio_input.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE && VOXONE_BT_I2S_RX_ENABLED

#include <driver/i2s.h>
#include <freertos/task.h>

#include "bt_audio_input_state.h"
#include "bt_runtime.h"
#include "bt_audio_route_state.h"
#include "player.h"
#include "serialcli.h"
#include "system_operation_state.h"
#include "../hardware/hardware_descriptor.h"

namespace {
constexpr i2s_port_t kBtI2sPort = I2S_NUM_1;
constexpr uint32_t kReportIntervalMs = 5000;
constexpr uint32_t kRetryIntervalMs = 5000;
constexpr uint32_t kReadWaitMs = 20;
constexpr uint32_t kWriteWaitMs = 10;
constexpr uint32_t kRouteLockWaitMs = 30;
constexpr uint32_t kTaskStackBytes = 4096;
constexpr UBaseType_t kTaskPriority = 2;
constexpr BaseType_t kTaskCore = 1;
}  // namespace

BtAudioInput btAudioInput;

void BtAudioInput::begin() {
  outputMutex_ = xSemaphoreCreateMutex();
  if (!outputMutex_)
    serialCli.printf("##[BT-AUDIO]# I2S0 route mutex creation failed\n");
  lastReportMs_ = millis();
}

bool BtAudioInput::blockForUpdate() {
  if (!btRuntime.physicalStarted()) return true;
  routeEnabled_.store(false);
  radioReady_.store(false);
  if (!outputMutex_ || xSemaphoreTake(outputMutex_, pdMS_TO_TICKS(100)) != pdTRUE)
    return false;
  const esp_err_t error = i2s_zero_dma_buffer(I2S_NUM_0);
  xSemaphoreGive(outputMutex_);
  return error == ESP_OK;
}

bool BtAudioInput::acquirePlayerOutput() {
  if (!btRuntime.physicalStarted()) return true;
  routeEnabled_.store(false);
  radioReady_.store(false);
  // Drain any in-flight BT write before Player resets its decoder/I2S clock.
  if (!outputMutex_ || xSemaphoreTake(outputMutex_, pdMS_TO_TICKS(100)) != pdTRUE)
    return false;
  const esp_err_t error = i2s_zero_dma_buffer(I2S_NUM_0);
  if (error == ESP_OK) {
    bluetoothSelected_ = false;
    outputRate_ = player.getSampleRate();
    radioReady_.store(true);
  }
  xSemaphoreGive(outputMutex_);
  return error == ESP_OK;
}

void BtAudioInput::route(bool bluetoothSelected, bool playbackPlaying,
                         uint32_t radioRate) {
  if (systemUpdateAudioBlocked()) {
    routeEnabled_.store(false);
    radioReady_.store(false);
    return;
  }
  // While RADIO owns I2S0, Audio::setSampleRate() already manages its clock.
  if (!bluetoothSelected && !bluetoothSelected_ && radioReady_.load()) {
    outputRate_ = radioRate;
    return;
  }
  const BtAudioRouteTarget target = btAudioRouteTarget(
      bluetoothSelected, playbackPlaying, active_ && taskRunning_.load() &&
          !stopRequested_.load(), sampleRate_, radioRate);
  const uint32_t targetRate = target.outputRate;
  const bool desiredBt = target.btOutput;
  if (bluetoothSelected == bluetoothSelected_ &&
      outputRate_ == targetRate && routeEnabled_.load() == desiredBt &&
      radioReady_.load() == target.radioOutput)
    return;

  routeEnabled_.store(false);
  radioReady_.store(false);
  if (!outputMutex_) {
    if (!bluetoothSelected) {
      bluetoothSelected_ = false;
      outputRate_ = radioRate;
      radioReady_.store(true);
    }
    return;
  }
  if (xSemaphoreTake(outputMutex_, pdMS_TO_TICKS(kRouteLockWaitMs)) != pdTRUE)
    return;

  const bool changedSource = bluetoothSelected != bluetoothSelected_;
  const bool changedRate = targetRate != outputRate_;
  esp_err_t error = ESP_OK;
  if (changedSource || changedRate) error = i2s_zero_dma_buffer(I2S_NUM_0);
  if (error == ESP_OK && targetRate != 0 && (changedSource || changedRate))
    error = i2s_set_sample_rates(I2S_NUM_0, targetRate);
  if (error == ESP_OK) {
    bluetoothSelected_ = bluetoothSelected;
    outputRate_ = targetRate;
    routeEnabled_.store(target.btOutput);
    radioReady_.store(target.radioOutput);
  }
  xSemaphoreGive(outputMutex_);
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
  readError_.store(0);
  sampleRate_ = rate;
  lastReportMs_ = nowMs;
  lastReportBytes_ = 0;
  lastReportTxBytes_ = 0;
  portENTER_CRITICAL(&statsMux_);
  stats_ = Stats{};
  portEXIT_CRITICAL(&statsMux_);
  taskRunning_.store(true);
  if (xTaskCreatePinnedToCore(readTask, "BtI2SRx", kTaskStackBytes, this,
                              kTaskPriority, nullptr, kTaskCore) != pdPASS) {
    taskRunning_.store(false);
    i2s_stop(kBtI2sPort);
    i2s_driver_uninstall(kBtI2sPort);
    serialCli.printf("##[BT-AUDIO]# I2S1 RX task creation failed\n");
    return false;
  }

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
  // The task's read has a bounded 20 ms wait. Only it calls i2s_read(), so
  // uninstall the driver after it has left its read loop.
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
    portENTER_CRITICAL(&statsMux_);
    stats_.bytesReceived += bytesRead;
    ++stats_.blocksReceived;
    stats_.lastDataMs = dataMs;
    if (blockPeak > stats_.peak16) stats_.peak16 = blockPeak;
    portEXIT_CRITICAL(&statsMux_);
    if (routeEnabled_.load() && !systemUpdateAudioBlocked() && outputMutex_ &&
        xSemaphoreTake(outputMutex_, pdMS_TO_TICKS(kWriteWaitMs)) == pdTRUE) {
      if (routeEnabled_.load() && !systemUpdateAudioBlocked()) {
        if (player.outputSilent()) memset(pcm, 0, bytesRead);
        size_t bytesWritten = 0;
        const esp_err_t writeError = i2s_write(I2S_NUM_0, pcm, bytesRead,
                                               &bytesWritten,
                                               pdMS_TO_TICKS(kWriteWaitMs));
        portENTER_CRITICAL(&statsMux_);
        stats_.bytesTransmitted += bytesWritten;
        if (bytesWritten) ++stats_.blocksTransmitted;
        if (writeError != ESP_OK) ++stats_.writeErrors;
        if (bytesWritten != bytesRead) ++stats_.shortWrites;
        portEXIT_CRITICAL(&statsMux_);
      }
      xSemaphoreGive(outputMutex_);
    } else if (routeEnabled_.load()) {
      portENTER_CRITICAL(&statsMux_);
      ++stats_.shortWrites;
      portEXIT_CRITICAL(&statsMux_);
    }
  }
  taskRunning_.store(false);
  vTaskDelete(nullptr);
}

void BtAudioInput::loop(const BtLinkState& link, bool bluetoothSelected,
                        uint32_t radioRate, uint32_t nowMs) {
  const uint32_t desiredRate = btAudioDesiredRate(link);
  const bool playbackPlaying = desiredRate != 0 &&
                               link.playback == BtPlayback::Playing;
  if (desiredRate == 0) {
    route(bluetoothSelected, false, radioRate);
    stop();
    retryPending_ = false;
    return;
  }
  if (active_ && stopRequested_.load() && !stop()) {
    route(bluetoothSelected, playbackPlaying, radioRate);
    return;
  }
  if (active_ && sampleRate_ != desiredRate) {
    route(false, false, radioRate);
    if (!stop()) return;
  }
  if (active_ && !taskRunning_.load()) {
    const int error = readError_.load();
    if (!stop()) return;
    if (error != 0)
      serialCli.printf("##[BT-AUDIO]# I2S1 read failed err=%d\n", error);
    retryPending_ = true;
    lastFailureMs_ = nowMs;
  }
  if (!active_) {
    if (retryPending_ && nowMs - lastFailureMs_ < kRetryIntervalMs) return;
    if (!start(desiredRate, nowMs)) {
      retryPending_ = true;
      lastFailureMs_ = nowMs;
      return;
    }
    retryPending_ = false;
  }

  route(bluetoothSelected, playbackPlaying, radioRate);

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
        "##[BT-AUDIO]# rate=%lu rx=%lu B/s tx=%lu B/s pct=%lu bytes=%llu txBytes=%llu rxBlocks=%lu txBlocks=%lu timeouts=%lu errors=%lu writeErrors=%lu shortWrites=%lu peak=%u lastDataMs=%lu\n",
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
        static_cast<unsigned long>(snapshot.shortWrites),
        static_cast<unsigned>(snapshot.peak16),
        static_cast<unsigned long>(snapshot.lastDataMs));
    lastReportBytes_ = snapshot.bytesReceived;
    lastReportTxBytes_ = snapshot.bytesTransmitted;
    lastReportMs_ = nowMs;
  }
}

#endif
