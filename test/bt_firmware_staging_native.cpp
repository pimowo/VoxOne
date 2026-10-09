#include <cassert>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../src/core/bt_firmware_staging.h"

namespace {
// Exact 60 bytes extracted from the V0 firmware.bin manifest at offset 3154.
const uint8_t kV0Manifest[60] = {
  0xcf,0x38,0x29,0x2c,0xc1,0xa1,0x48,0x4c,0xb9,0xb3,0xf5,0x57,0xfd,0xea,0x56,0x0a,
  0x01,0x00,0x3c,0x00,0x01,0x54,0x42,0x56,0x56,0x00,0x01,0x00,0x02,0x00,0x00,0x00,
  '0','.','6','.','1','-','d','e','v',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0xc8,0x6f,0xbc,0xe5
};
struct FakeHeap {
  size_t live = 0;
  size_t calls = 0;
  size_t failAt = 0;
  static uint8_t* alloc(size_t size, void* ctx) {
    auto& heap = *static_cast<FakeHeap*>(ctx);
    if (heap.failAt && ++heap.calls == heap.failAt) return nullptr;
    ++heap.live;
    return static_cast<uint8_t*>(malloc(size));
  }
  static void release(uint8_t* data, void* ctx) {
    --static_cast<FakeHeap*>(ctx)->live;
    free(data);
  }
};
using Error = BtFirmwareStaging::Error;
void refreshManifestCrc(std::vector<uint8_t>& image, size_t offset) {
  uint32_t crc = BtFirmwareStaging::crc32Update(0xffffffffu, image.data() + offset, 56) ^ 0xffffffffu;
  for (size_t i = 0; i < 4; ++i) image[offset + 56 + i] = static_cast<uint8_t>(crc >> (8 * i));
}
std::vector<uint8_t> image(size_t size = 101, size_t offset = 18) {
  std::vector<uint8_t> bytes(size, 0x71);
  bytes[0] = 0xe9;
  memcpy(bytes.data() + offset, kV0Manifest, sizeof(kV0Manifest));
  return bytes;
}
Error validate(const std::vector<uint8_t>& bytes) {
  FakeHeap heap;
  BtFirmwareStaging staging(FakeHeap::alloc, FakeHeap::release, &heap);
  assert(staging.prepare(bytes.size(), 2 * 1024 * 1024, true, true) == Error::None);
  assert(staging.append(0, bytes.data(), bytes.size()) == Error::None);
  const Error result = staging.finalize();
  assert(heap.live > 0);
  staging.clear();
  assert(heap.live == 0);
  return result;
}
}  // namespace

int main() {
  assert(BtFirmwareStaging::uploadSlotAvailable(false, false, false));
  assert(!BtFirmwareStaging::uploadSlotAvailable(true, false, false));  // concurrent HTTP upload
  assert(!BtFirmwareStaging::uploadSlotAvailable(false, true, false));  // queued image
  assert(!BtFirmwareStaging::uploadSlotAvailable(false, false, true));  // active sender
  assert((BtFirmwareStaging::crc32Update(0xffffffffu,
      reinterpret_cast<const uint8_t*>("123456789"), 9) ^ 0xffffffffu) == 0xcbf43926u);
  FakeHeap heap;
  BtFirmwareStaging stage(FakeHeap::alloc, FakeHeap::release, &heap);
  assert(stage.prepare(0, 8 * 1024 * 1024, true, true) == Error::InvalidSize);
  assert(stage.prepare(BtFirmwareStaging::MaxImageSize + 1, 8 * 1024 * 1024, true, true) == Error::InvalidSize);
  assert(stage.prepare(101, 8 * 1024 * 1024, false, true) == Error::Unsupported); // X0
  assert(stage.prepare(101, 0, true, false) == Error::NoPsram);
  assert(stage.prepare(1310720, 1500000, true, true) == Error::InsufficientPsram);
  assert(stage.prepare(101, BtFirmwareStaging::PsramReserve + 100, true, true) == Error::InsufficientPsram);
  assert(stage.prepare(101, 8 * 1024 * 1024, true, true) == Error::None);
  assert(stage.finalize() == Error::Incomplete);
  stage.clear();
  assert(heap.live == 0);

  // 2 MB Bx budget with 384 KiB retained, exact V0-slot maximum, 80 blocks.
  assert(stage.prepare(BtFirmwareStaging::MaxImageSize, 2 * 1024 * 1024, true, true) == Error::None);
  assert(heap.live == BtFirmwareStaging::MaxBlocks);
  auto maximum = image(BtFirmwareStaging::MaxImageSize, BtFirmwareStaging::BlockSize - 20);
  for (size_t offset = 0; offset < maximum.size(); offset += 4096) {
    const size_t chunk = std::min<size_t>(4096, maximum.size() - offset);
    assert(stage.append(offset, maximum.data() + offset, chunk) == Error::None);
  }
  assert(stage.finalize() == Error::None);
  assert(stage.size() == BtFirmwareStaging::MaxImageSize);
  stage.clear();
  assert(heap.live == 0);
  heap.failAt = heap.calls + 3;
  assert(stage.prepare(BtFirmwareStaging::MaxImageSize, 8 * 1024 * 1024, true, true) == Error::Allocation);
  assert(heap.live == 0);
  heap.failAt = 0;

  auto bytes = image(BtFirmwareStaging::BlockSize + 100, BtFirmwareStaging::BlockSize - 20);
  assert(stage.prepare(bytes.size(), 8 * 1024 * 1024, true, true) == Error::None);
  assert(stage.append(0, bytes.data(), BtFirmwareStaging::BlockSize - 5) == Error::None);
  assert(stage.append(BtFirmwareStaging::BlockSize - 4, bytes.data(), 1) == Error::Offset);
  assert(stage.append(BtFirmwareStaging::BlockSize - 5,
      bytes.data() + BtFirmwareStaging::BlockSize - 5, bytes.size() - (BtFirmwareStaging::BlockSize - 5)) == Error::None);
  assert(stage.finalize() == Error::None);
  assert(std::strcmp(stage.version(), "0.6.1-dev") == 0);
  const uint32_t expectedCrc = BtFirmwareStaging::crc32Update(0xffffffffu, bytes.data(), bytes.size()) ^ 0xffffffffu;
  assert(stage.crc32() == expectedCrc);
  uint8_t window[60]{};
  assert(stage.read(BtFirmwareStaging::BlockSize - 20, window, sizeof(window)) == sizeof(window));
  assert(memcmp(window, kV0Manifest, sizeof(window)) == 0);
  memset(window, 0, sizeof(window));
  assert(stage.read(BtFirmwareStaging::BlockSize - 20, window, sizeof(window)) == sizeof(window));
  assert(memcmp(window, kV0Manifest, sizeof(window)) == 0);
  stage.clear();  // Abort/rejected sender/terminal release: same owner cleanup path.
  assert(heap.live == 0);
  for (int terminal = 0; terminal < 4; ++terminal) {
    assert(stage.prepare(bytes.size(), 8 * 1024 * 1024, true, true) == Error::None);
    assert(stage.append(0, bytes.data(), bytes.size()) == Error::None);
    assert(stage.finalize() == Error::None);
    assert(heap.live == 2); // Retained for retry/read until sender rejects or terminates.
    assert(stage.read(BtFirmwareStaging::BlockSize - 20, window, 60) == 60);
    stage.clear(); // rejected start, SUCCESS, ERROR, ABORT
    assert(heap.live == 0);
  }
  assert(stage.prepare(bytes.size(), 8 * 1024 * 1024, true, true) == Error::None);
  assert(stage.append(0, bytes.data(), 100) == Error::None);
  stage.clear(); // Interrupted HTTP request; no finalized image reaches sender.
  assert(heap.live == 0 && !stage.ready());

  assert(validate(image()) == Error::None);
  auto mutated = image(); mutated[18 + 32 + 4] = '2';
  refreshManifestCrc(mutated, 18);
  assert(stage.prepare(mutated.size(), 8 * 1024 * 1024, true, true) == Error::None);
  assert(stage.append(0, mutated.data(), mutated.size()) == Error::None);
  assert(stage.finalize() == Error::None);
  assert(std::strcmp(stage.version(), "0.6.2-dev") == 0);
  stage.clear();
  mutated = image(); mutated[18] = 0; assert(validate(mutated) == Error::ManifestCount); // magic
  mutated = image(); mutated[34] ^= 1; assert(validate(mutated) == Error::ManifestCount); // CRC
  const size_t fields[] = {34, 36, 38, 42, 43, 44, 46};
  const uint8_t wrong[] = {2, 59, 2, 'A', 1, 2, 3};
  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
    mutated = image(); mutated[fields[i]] = wrong[i]; refreshManifestCrc(mutated, 18);
    assert(validate(mutated) == Error::ManifestCount);
  }
  mutated = image(); mutated[50] = 0; refreshManifestCrc(mutated, 18);
  assert(validate(mutated) == Error::ManifestCount); // empty version
  mutated = image(); memset(mutated.data() + 50, 'a', 24); refreshManifestCrc(mutated, 18);
  assert(validate(mutated) == Error::ManifestCount); // unterminated
  mutated = image(); mutated[50 + 22] = 'x'; refreshManifestCrc(mutated, 18);
  assert(validate(mutated) == Error::ManifestCount); // nonzero padding
  mutated = image(170, 18); memcpy(mutated.data() + 90, kV0Manifest, 60);
  assert(validate(mutated) == Error::ManifestCount); // duplicate valid
  mutated = image(100, 18); memcpy(mutated.data() + 84, kV0Manifest, 16);
  assert(validate(mutated) == Error::None); // truncated candidate after valid
  mutated = image(); memset(mutated.data() + 18, 0, 60);
  assert(validate(mutated) == Error::ManifestCount);
}
