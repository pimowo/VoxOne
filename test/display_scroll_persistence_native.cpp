#include "../src/core/display_scroll_persistence.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>

using namespace voxone::display_scroll;

namespace {

std::size_t allocationCount = 0;

class FakeStorage : public ScrollSettingsStorage {
 public:
  ScrollSettingsStorageReadStatus read(uint8_t* output, size_t capacity,
                                       size_t& outputSize) override {
    ++reads;
    if (readStatus != ScrollSettingsStorageReadStatus::Found) {
      outputSize = 0;
      return readStatus;
    }
    outputSize = size;
    if (size <= capacity && size) std::memcpy(output, bytes, size);
    return ScrollSettingsStorageReadStatus::Found;
  }

  ScrollSettingsStorageWriteStatus write(const uint8_t* input,
                                          size_t inputSize) override {
    ++writes;
    if (writeStatus != ScrollSettingsStorageWriteStatus::Stored)
      return writeStatus;
    assert(input && inputSize <= sizeof(bytes));
    std::memcpy(bytes, input, inputSize);
    size = inputSize;
    readStatus = ScrollSettingsStorageReadStatus::Found;
    return ScrollSettingsStorageWriteStatus::Stored;
  }

  void setRecord(const uint8_t* input, size_t inputSize) {
    assert(inputSize <= sizeof(bytes));
    if (inputSize) std::memcpy(bytes, input, inputSize);
    size = inputSize;
    readStatus = ScrollSettingsStorageReadStatus::Found;
  }

  uint8_t bytes[32]{};
  size_t size = 0;
  unsigned reads = 0;
  unsigned writes = 0;
  ScrollSettingsStorageReadStatus readStatus =
      ScrollSettingsStorageReadStatus::Missing;
  ScrollSettingsStorageWriteStatus writeStatus =
      ScrollSettingsStorageWriteStatus::Stored;
};

void write32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t independentCrc32(const uint8_t* input, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t index = 0; index < size; ++index) {
    crc ^= input[index];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc ^ 0xffffffffu;
}

void refreshCrc(uint8_t* record) {
  write32(record + 9, independentCrc32(record, 9));
}

void expectRendererDefaults(const ScrollSettingsLoadResult& result,
                            ScrollSettingsLoadStatus status) {
  assert(result.source == ScrollSettingsSource::RendererDefaults);
  assert(result.status == status);
  assert(equalScrollSettings(result.settings, defaultScrollSettings()));

  const ResolvedScrollParameters oled = resolveScrollParameters(
      ScrollRendererProfile::Oled128x64, result.settings, result.source);
  const ResolvedScrollParameters st7789 = resolveScrollParameters(
      ScrollRendererProfile::St7789_284x76, result.settings, result.source);
  const ResolvedScrollParameters st7796 = resolveScrollParameters(
      ScrollRendererProfile::St7796_480x320, result.settings, result.source);
  assert(oled.enabled && oled.pauseMs == 5000 &&
         oled.stepPixels == 2 && oled.intervalMs == 35);
  assert(st7789.enabled && st7789.pauseMs == 5000 &&
         st7789.stepPixels == 1 && st7789.intervalMs == 9);
  assert(st7796.enabled && st7796.pauseMs == 2500 &&
         st7796.stepPixels == 2 && st7796.intervalMs == 50);
}

void testMissingAndRestart() {
  FakeStorage storage;
  const ScrollSettingsLoadResult missing = loadScrollSettings(storage);
  expectRendererDefaults(missing,
                         ScrollSettingsLoadStatus::RendererDefaultsMissing);
  assert(missing.recordStatus == ScrollSettingsRecordStatus::NotDecoded);
  assert(storage.reads == 1 && storage.writes == 0);

  const ScrollSettings wanted{false, ScrollSpeed::Fast, 3000};
  assert(saveScrollSettings(storage, missing, wanted) ==
         ScrollSettingsSaveStatus::Saved);
  assert(storage.writes == 1 && storage.size == kScrollSettingsRecordSize);

  // A fresh load simulates restart; no process-local cache is required.
  const ScrollSettingsLoadResult restarted = loadScrollSettings(storage);
  assert(restarted.source == ScrollSettingsSource::UserSettings);
  assert(restarted.status == ScrollSettingsLoadStatus::UserSettings);
  assert(restarted.recordStatus == ScrollSettingsRecordStatus::Valid);
  assert(equalScrollSettings(restarted.settings, wanted));
  assert(!restarted.settings.enabled);

  const unsigned writesBefore = storage.writes;
  assert(saveScrollSettings(storage, restarted, wanted) ==
         ScrollSettingsSaveStatus::Unchanged);
  assert(storage.writes == writesBefore);
}

void testWireFormatAndAllValues() {
  const ScrollSpeed speeds[] = {
      ScrollSpeed::Slow, ScrollSpeed::Normal, ScrollSpeed::Fast};
  const uint16_t pauses[] = {0, 1000, 2000, 3000, 4000, 5000};

  for (ScrollSpeed speed : speeds) {
    for (uint16_t pause : pauses) {
      FakeStorage storage;
      const ScrollSettingsLoadResult missing = loadScrollSettings(storage);
      const ScrollSettings wanted{true, speed, pause};
      assert(saveScrollSettings(storage, missing, wanted) ==
             ScrollSettingsSaveStatus::Saved);
      assert(std::memcmp(storage.bytes, "VXSC", 4) == 0);
      assert(storage.bytes[4] == kScrollSettingsRecordVersion);
      assert(storage.bytes[5] == 1);
      assert(storage.bytes[6] == static_cast<uint8_t>(speed));
      assert(storage.bytes[7] == static_cast<uint8_t>(pause));
      assert(storage.bytes[8] == static_cast<uint8_t>(pause >> 8));
      assert(scrollSettingsRecordCrc32(storage.bytes, 9) ==
             independentCrc32(storage.bytes, 9));
      const ScrollSettingsLoadResult loaded = loadScrollSettings(storage);
      assert(loaded.source == ScrollSettingsSource::UserSettings);
      assert(equalScrollSettings(loaded.settings, wanted));
    }
  }

  uint8_t record[kScrollSettingsRecordSize]{};
  assert(!serializeScrollSettingsRecord(
      ScrollSettings{true, ScrollSpeed::Normal, 2500}, record,
      sizeof(record)));
  assert(!serializeScrollSettingsRecord(
      ScrollSettings{true, static_cast<ScrollSpeed>(9), 5000}, record,
      sizeof(record)));
  assert(!serializeScrollSettingsRecord(defaultScrollSettings(), record,
                                        sizeof(record) - 1));
}

void expectInvalidRecord(uint8_t* record, size_t size,
                         ScrollSettingsRecordStatus expected) {
  FakeStorage storage;
  storage.setRecord(record, size);
  const ScrollSettingsLoadResult loaded = loadScrollSettings(storage);
  expectRendererDefaults(loaded,
                         ScrollSettingsLoadStatus::RendererDefaultsInvalid);
  assert(loaded.recordStatus == expected);
  assert(storage.writes == 0);
}

void testInvalidRecordsDoNotRewrite() {
  uint8_t valid[kScrollSettingsRecordSize]{};
  assert(serializeScrollSettingsRecord(defaultScrollSettings(), valid,
                                       sizeof(valid)));

  uint8_t damaged[kScrollSettingsRecordSize + 1]{};
  std::memcpy(damaged, valid, sizeof(valid));
  damaged[5] ^= 1;
  expectInvalidRecord(damaged, sizeof(valid),
                      ScrollSettingsRecordStatus::InvalidCrc);

  std::memcpy(damaged, valid, sizeof(valid));
  damaged[9] ^= 1;
  expectInvalidRecord(damaged, sizeof(valid),
                      ScrollSettingsRecordStatus::InvalidCrc);

  expectInvalidRecord(valid, sizeof(valid) - 1,
                      ScrollSettingsRecordStatus::InvalidSize);
  std::memcpy(damaged, valid, sizeof(valid));
  damaged[sizeof(valid)] = 0xaa;
  expectInvalidRecord(damaged, sizeof(damaged),
                      ScrollSettingsRecordStatus::InvalidSize);

  std::memcpy(damaged, valid, sizeof(valid));
  damaged[0] = 'Q';
  refreshCrc(damaged);
  expectInvalidRecord(damaged, sizeof(valid),
                      ScrollSettingsRecordStatus::InvalidMagic);

  std::memcpy(damaged, valid, sizeof(valid));
  damaged[4] = kScrollSettingsRecordVersion + 1;
  refreshCrc(damaged);
  expectInvalidRecord(damaged, sizeof(valid),
                      ScrollSettingsRecordStatus::InvalidVersion);

  std::memcpy(damaged, valid, sizeof(valid));
  damaged[5] = 2;
  refreshCrc(damaged);
  expectInvalidRecord(damaged, sizeof(valid),
                      ScrollSettingsRecordStatus::InvalidEnabled);

  std::memcpy(damaged, valid, sizeof(valid));
  damaged[6] = 9;
  refreshCrc(damaged);
  expectInvalidRecord(damaged, sizeof(valid),
                      ScrollSettingsRecordStatus::InvalidSpeed);

  std::memcpy(damaged, valid, sizeof(valid));
  damaged[7] = 0xc4;  // 2500 ms, little endian.
  damaged[8] = 0x09;
  refreshCrc(damaged);
  expectInvalidRecord(damaged, sizeof(valid),
                      ScrollSettingsRecordStatus::InvalidPause);
}

void testStorageErrorsAndExplicitSave() {
  FakeStorage storage;
  storage.readStatus = ScrollSettingsStorageReadStatus::OpenError;
  expectRendererDefaults(loadScrollSettings(storage),
                         ScrollSettingsLoadStatus::RendererDefaultsOpenError);
  assert(storage.writes == 0);
  storage.readStatus = ScrollSettingsStorageReadStatus::ReadError;
  expectRendererDefaults(loadScrollSettings(storage),
                         ScrollSettingsLoadStatus::RendererDefaultsReadError);
  assert(storage.writes == 0);

  const ScrollSettingsLoadResult defaults =
      ScrollSettingsLoadResult{defaultScrollSettings(),
                               ScrollSettingsSource::RendererDefaults,
                               ScrollSettingsLoadStatus::RendererDefaultsMissing,
                               ScrollSettingsRecordStatus::NotDecoded};
  const ScrollSettings wanted{true, ScrollSpeed::Slow, 1000};
  struct FailureCase {
    ScrollSettingsStorageWriteStatus storage;
    ScrollSettingsSaveStatus expected;
  };
  const FailureCase failures[] = {
      {ScrollSettingsStorageWriteStatus::OpenError,
       ScrollSettingsSaveStatus::StorageOpenError},
      {ScrollSettingsStorageWriteStatus::WriteError,
       ScrollSettingsSaveStatus::StorageWriteError},
      {ScrollSettingsStorageWriteStatus::CommitError,
       ScrollSettingsSaveStatus::StorageCommitError},
      {ScrollSettingsStorageWriteStatus::VerifyError,
       ScrollSettingsSaveStatus::StorageVerifyError}};
  for (const FailureCase& failure : failures) {
    FakeStorage failing;
    failing.writeStatus = failure.storage;
    assert(saveScrollSettings(failing, defaults, wanted) == failure.expected);
    assert(failing.writes == 1 && failing.size == 0);
  }

  FakeStorage explicitDefault;
  const ScrollSettingsLoadResult missing = loadScrollSettings(explicitDefault);
  assert(saveScrollSettings(explicitDefault, missing, defaultScrollSettings()) ==
         ScrollSettingsSaveStatus::Saved);
  assert(explicitDefault.writes == 1);

  const ScrollSettings invalidSpeed{
      true, static_cast<ScrollSpeed>(0xff), 5000};
  assert(saveScrollSettings(explicitDefault, missing, invalidSpeed) ==
         ScrollSettingsSaveStatus::Invalid);
  assert(saveScrollSettings(
             explicitDefault, missing,
             ScrollSettings{true, ScrollSpeed::Normal, 2500}) ==
         ScrollSettingsSaveStatus::Invalid);
  assert(explicitDefault.writes == 1);
}

void testFixedCostAndSafeSnapshot() {
  FakeStorage storage;
  const std::size_t allocationsBefore = allocationCount;
  ScrollSettingsLoadResult current = loadScrollSettings(storage);
  for (uint16_t pause = 0; pause <= 5000; pause += 1000) {
    const ScrollSettings requested{
        pause != 0, ScrollSpeed::Normal, pause};
    assert(saveScrollSettings(storage, current, requested) ==
           ScrollSettingsSaveStatus::Saved);
    current = loadScrollSettings(storage);
    assert(equalScrollSettings(current.settings, requested));
  }
  assert(allocationCount == allocationsBefore);
  static_assert(sizeof(ScrollSettingsLoadResult) <= 8,
                "scroll settings snapshot unexpectedly grew");
}

}  // namespace

void* operator new(std::size_t size) {
  ++allocationCount;
  if (void* memory = std::malloc(size)) return memory;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
  ++allocationCount;
  if (void* memory = std::malloc(size)) return memory;
  throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }

int main() {
  testMissingAndRestart();
  testWireFormatAndAllValues();
  testInvalidRecordsDoNotRewrite();
  testStorageErrorsAndExplicitSave();
  testFixedCostAndSafeSnapshot();
  return 0;
}
