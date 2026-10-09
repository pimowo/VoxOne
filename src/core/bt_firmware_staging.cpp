#include "bt_firmware_staging.h"
#include <algorithm>
#include <string.h>

namespace {
// IMAGE_MANIFEST.md / VoxOneImageManifest v1: 60 bytes, little-endian fields.
constexpr uint8_t kMagic[16] = {
    0xcf, 0x38, 0x29, 0x2c, 0xc1, 0xa1, 0x48, 0x4c,
    0xb9, 0xb3, 0xf5, 0x57, 0xfd, 0xea, 0x56, 0x0a};
constexpr size_t kManifestSize = 60;
uint16_t read16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1]) << 8;
}
uint32_t read32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
         static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}
bool validManifest(const uint8_t* p) {
  if (memcmp(p, kMagic, sizeof(kMagic)) || read16(p + 16) != 1 ||
      read16(p + 18) != kManifestSize || read32(p + 20) != 0x56425401u ||
      p[24] != 'V' || p[25] != 0 || read16(p + 26) != 1 || p[28] != 2 ||
      p[29] || p[30] || p[31]) return false;
  bool terminated = false;
  for (size_t i = 32; i < 56; ++i) {
    const uint8_t c = p[i];
    if (terminated) { if (c) return false; }
    else if (!c) { if (i == 32) return false; terminated = true; }
    else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-'))
      return false;
  }
  if (!terminated) return false;
  return read32(p + 56) ==
      (BtFirmwareStaging::crc32Update(0xffffffffu, p, 56) ^ 0xffffffffu);
}
}  // namespace

BtFirmwareStaging::BtFirmwareStaging(Alloc alloc, Free free, void* context)
    : alloc_(alloc), free_(free), context_(context) {}
BtFirmwareStaging::~BtFirmwareStaging() { clear(); }

uint32_t BtFirmwareStaging::crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc;
}

void BtFirmwareStaging::clear() {
  for (size_t i = 0; i < blockCount_; ++i) {
    if (blocks_[i]) free_(blocks_[i], context_);
    blocks_[i] = nullptr;
  }
  blockCount_ = 0;
  expected_ = received_ = crc_ = 0;
  crcState_ = 0xffffffffu;
  version_[0] = '\0';
  ready_ = false;
}

BtFirmwareStaging::Error BtFirmwareStaging::prepare(
    uint32_t expected, uint32_t freePsram, bool supported, bool psramPresent) {
  clear();
  if (!supported) return Error::Unsupported;
  if (!psramPresent) return Error::NoPsram;
  if (!expected || expected > MaxImageSize) return Error::InvalidSize;
  if (freePsram < PsramReserve || expected > freePsram - PsramReserve)
    return Error::InsufficientPsram;
  const size_t count = (expected + BlockSize - 1) / BlockSize;
  for (size_t i = 0; i < count; ++i) {
    const size_t length = std::min<uint32_t>(BlockSize, expected - i * BlockSize);
    uint8_t* block = alloc_(length, context_);
    if (!block) { clear(); return Error::Allocation; }
    blocks_[blockCount_++] = block;
  }
  expected_ = expected;
  return Error::None;
}

BtFirmwareStaging::Error BtFirmwareStaging::append(
    uint32_t offset, const uint8_t* data, size_t length) {
  if (!expected_ || ready_ || !data || offset != received_ ||
      length > expected_ - received_) return Error::Offset;
  size_t copied = 0;
  while (copied < length) {
    const uint32_t position = received_ + copied;
    const size_t n = std::min<size_t>(length - copied, BlockSize - position % BlockSize);
    memcpy(blocks_[position / BlockSize] + position % BlockSize, data + copied, n);
    copied += n;
  }
  crcState_ = crc32Update(crcState_, data, length);
  received_ += length;
  return Error::None;
}

size_t BtFirmwareStaging::read(uint32_t offset, uint8_t* output, size_t length) {
  if (!expected_ || !output || offset > received_ || length > received_ - offset) return 0;
  size_t copied = 0;
  while (copied < length) {
    const uint32_t position = offset + copied;
    const size_t n = std::min<size_t>(length - copied, BlockSize - position % BlockSize);
    memcpy(output + copied, blocks_[position / BlockSize] + position % BlockSize, n);
    copied += n;
  }
  return copied;
}

BtFirmwareStaging::Error BtFirmwareStaging::finalize() {
  if (!expected_ || received_ != expected_) return Error::Incomplete;
  uint8_t first = 0;
  if (read(0, &first, 1) != 1 || first != 0xe9) return Error::ImageMagic;
  // Scan every possible offset; no linked-image layout or app descriptor assumption.
  uint8_t candidate[kManifestSize];
  size_t validCount = 0;
  for (uint32_t offset = 0; offset + kManifestSize <= expected_; ++offset) {
    const uint8_t initial = blocks_[offset / BlockSize][offset % BlockSize];
    if (initial != kMagic[0]) continue;
    if (read(offset, candidate, sizeof(candidate)) != sizeof(candidate)) continue;
    if (!validManifest(candidate)) continue;
    if (++validCount > 1) return Error::ManifestCount;
    memcpy(version_, candidate + 32, sizeof(version_));
  }
  if (validCount != 1) return Error::ManifestCount;
  crc_ = crcState_ ^ 0xffffffffu;
  ready_ = true;
  return Error::None;
}
