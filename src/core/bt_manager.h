#ifndef VOXONE_BT_MANAGER_H
#define VOXONE_BT_MANAGER_H

// Consolidated from bt_runtime.h.
#include <atomic>

#include "bt_link_protocol.h"

// BT hardware, user enablement, module availability and phone connection
// are separate facts. Physical initialization happens only during setup.
struct BtRuntimeStatus {
  bool supportsBt;
  bool btEnabled;
  bool btOnline;
  bool btConnected;
};

class BtRuntime {
 public:
  explicit BtRuntime(bool supportsBt)
      : supportsBt_(supportsBt), btEnabled_(supportsBt) {}

  // May be called only before start(); no persistent setting is stored.
  bool configureBeforeStart(bool enabled) {
    if (started_) return false;
    btEnabled_.store(enabled);
    return true;
  }

  bool start() {
    if (started_) return physicalStarted_;
    started_ = true;
    physicalStarted_ = supportsBt_ && btEnabled_.load();
    return physicalStarted_;
  }

  // Runtime toggles affect source availability. A boot without BT does not
  // acquire UART/I2S later; that requires a separate hot-start lifecycle.
  void setEnabled(bool enabled) { btEnabled_.store(enabled); }
  bool physicalStarted() const { return physicalStarted_; }
  bool available() const { return physicalStarted_ && btEnabled_.load(); }

  const BtLinkState& effectiveLinkState(const BtLinkState& raw) const {
    return available() ? raw : unavailableLink_;
  }

  BtRuntimeStatus status(bool moduleOnline, bool phoneConnected) const {
    const bool online = available() && moduleOnline;
    return {supportsBt_, btEnabled_.load(), online, online && phoneConnected};
  }

 private:
  bool supportsBt_;
  std::atomic<bool> btEnabled_;
  bool started_ = false;
  bool physicalStarted_ = false;
  BtLinkState unavailableLink_{};
};

extern BtRuntime btRuntime;

// Consolidated from bt_audio_input_state.h.
#include "bt_link_protocol.h"

// Zero means that I2S RX must be stopped. Only the formats confirmed for
// VoxOneBT's 16-bit stereo PCM link are enabled in this diagnostic stage.
inline uint32_t btAudioDesiredRate(const BtLinkState& link) {
  if (!link.runtimeAvailable || !link.connected) return 0;
  return link.sampleRate == 44100 || link.sampleRate == 48000
             ? link.sampleRate
             : 0;
}

// Consolidated from bt_firmware_staging.h.
#include "bt_firmware_sender.h"
#include <stddef.h>
#include <stdint.h>

// VoxOneBT V0 protocol/partition limit, not a universal V-family image limit.
class BtFirmwareStaging : public BtFirmwareImage {
 public:
  static constexpr uint32_t BlockSize = 16 * 1024;
  static constexpr uint32_t MaxImageSize = 1310720;
  static constexpr uint32_t PsramReserve = 384 * 1024;
  static constexpr size_t MaxBlocks = MaxImageSize / BlockSize;
  using Alloc = uint8_t* (*)(size_t, void*);
  using Free = void (*)(uint8_t*, void*);
  enum class Error : uint8_t {
    None, Unsupported, NoPsram, InvalidSize, InsufficientPsram,
    Allocation, Offset, Incomplete, ImageMagic, ManifestCount
  };

  BtFirmwareStaging(Alloc alloc, Free free, void* context);
  ~BtFirmwareStaging() override;
  BtFirmwareStaging(const BtFirmwareStaging&) = delete;
  BtFirmwareStaging& operator=(const BtFirmwareStaging&) = delete;
  Error prepare(uint32_t expected, uint32_t freePsram, bool supported, bool psramPresent);
  Error append(uint32_t offset, const uint8_t* data, size_t length);
  Error finalize();
  void clear();
  uint32_t size() const override { return expected_; }
  uint32_t crc32() const override { return crc_; }
  const char* version() const override { return version_; }
  size_t read(uint32_t offset, uint8_t* output, size_t length) override;
  uint32_t received() const { return received_; }
  bool ready() const { return ready_; }
  static bool uploadSlotAvailable(bool receiving, bool pending, bool senderActive) {
    return !receiving && !pending && !senderActive;
  }
  static uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length);

 private:
  Alloc alloc_;
  Free free_;
  void* context_;
  uint8_t* blocks_[MaxBlocks]{};
  size_t blockCount_ = 0;
  uint32_t expected_ = 0;
  uint32_t received_ = 0;
  uint32_t crc_ = 0;
  uint32_t crcState_ = 0xffffffffu;
  char version_[24]{};
  bool ready_ = false;
};

#endif  // VOXONE_BT_MANAGER_H
