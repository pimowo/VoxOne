#ifndef VOXONE_BT_FIRMWARE_STAGING_H
#define VOXONE_BT_FIRMWARE_STAGING_H

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

#endif
