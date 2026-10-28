#include "../src/displays/ch1116g_driver.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace voxone::displays::ch1116g;

namespace {

struct Transaction {
  uint8_t address;
  uint8_t control;
  std::vector<uint8_t> data;
};

class FakeTransport : public Transport {
 public:
  bool probe(uint8_t address) override {
    probedAddress = address;
    return probeResult;
  }

  bool write(uint8_t address, uint8_t control, const uint8_t* data,
             size_t size) override {
    const size_t current = writeCount++;
    if (current == failOnWrite) return false;
    transactions.push_back(
        Transaction{address, control, std::vector<uint8_t>(data, data + size)});
    return true;
  }

  void delayMilliseconds(uint16_t milliseconds) override {
    delays.push_back(milliseconds);
  }

  void resetLog() {
    transactions.clear();
    delays.clear();
    writeCount = 0;
    failOnWrite = static_cast<size_t>(-1);
  }

  bool probeResult = true;
  uint8_t probedAddress = 0;
  size_t writeCount = 0;
  size_t failOnWrite = static_cast<size_t>(-1);
  std::vector<Transaction> transactions;
  std::vector<uint16_t> delays;
};

bool containsPair(const std::vector<uint8_t>& data, uint8_t first,
                  uint8_t second) {
  for (size_t index = 0; index + 1 < data.size(); ++index)
    if (data[index] == first && data[index + 1] == second) return true;
  return false;
}

void verifyFrame(const std::vector<Transaction>& transactions,
                 const uint8_t* expected, uint8_t address) {
  size_t transaction = 0;
  size_t dataBytes = 0;
  for (uint8_t page = 0; page < kPages; ++page) {
    assert(transaction < transactions.size());
    const Transaction& pageAddress = transactions[transaction++];
    assert(pageAddress.address == address && pageAddress.control == 0x00);
    const std::vector<uint8_t> expectedAddress = {
        static_cast<uint8_t>(0xb0 | page), 0x02, 0x10};
    assert(pageAddress.data == expectedAddress);
    for (size_t offset = 0; offset < kWidth; offset += kMaxI2cDataChunk) {
      assert(transaction < transactions.size());
      const Transaction& payload = transactions[transaction++];
      assert(payload.address == address && payload.control == 0x40);
      assert(payload.data.size() <= kMaxI2cDataChunk);
      assert(std::equal(payload.data.begin(), payload.data.end(),
                        expected + static_cast<size_t>(page) * kWidth +
                            offset));
      dataBytes += payload.data.size();
    }
  }
  assert(transaction == transactions.size());
  assert(dataBytes == kBufferSize);
}

}  // namespace

int main() {
  static_assert(kWidth == 128 && kHeight == 64 && kPages == 8,
                "CH1116G geometry must stay 128x64");
  static_assert(kRamColumns == 132 && kDefaultColumnOffset == 2,
                "128 visible columns are SEG2..SEG129 in 132-column RAM");
  static_assert(kBufferSize == 1024, "framebuffer is exactly 128x64 bits");

  FakeTransport transport;
  Driver driver(transport);
  assert(driver.begin());
  assert(driver.ready());
  assert(transport.probedAddress == 0x3c);
  assert(transport.delays.size() == 1 && transport.delays[0] == 100);
  assert(!transport.transactions.empty());
  const Transaction& init = transport.transactions.front();
  assert(init.address == 0x3c && init.control == 0x00);
  assert(init.data.front() == 0xae);
  assert(containsPair(init.data, 0xd5, 0x50));
  assert(containsPair(init.data, 0xa8, 0x3f));
  assert(containsPair(init.data, 0xad, 0x8b));
  assert(containsPair(init.data, 0xda, 0x12));
  assert(containsPair(init.data, 0x81, 0x80));
  assert(containsPair(init.data, 0xdb, 0x35));
  assert(init.data.back() == 0x2e);
  assert(transport.transactions.back().data == std::vector<uint8_t>{0xaf});

  std::vector<uint8_t> cleared(kBufferSize, 0);
  std::vector<Transaction> initialFrame(transport.transactions.begin() + 1,
                                        transport.transactions.end() - 1);
  verifyFrame(initialFrame, cleared.data(), 0x3c);

  for (size_t index = 0; index < kBufferSize; ++index)
    driver.buffer()[index] = static_cast<uint8_t>((index * 37u) & 0xffu);
  std::vector<uint8_t> expected(driver.buffer(),
                                driver.buffer() + kBufferSize);
  transport.resetLog();
  assert(driver.display());
  verifyFrame(transport.transactions, expected.data(), 0x3c);

  const std::vector<uint8_t> beforeBounds(driver.buffer(),
                                          driver.buffer() + kBufferSize);
  driver.drawPixel(-1, 0, PixelMode::White);
  driver.drawPixel(128, 0, PixelMode::White);
  driver.drawPixel(0, -1, PixelMode::White);
  driver.drawPixel(0, 64, PixelMode::White);
  assert(std::equal(beforeBounds.begin(), beforeBounds.end(), driver.buffer()));
  driver.clear();
  driver.drawPixel(3, 9, PixelMode::White);
  assert(driver.buffer()[kWidth + 3] == 0x02);
  driver.drawPixel(3, 9, PixelMode::Invert);
  assert(driver.buffer()[kWidth + 3] == 0x00);
  driver.drawPixel(3, 9, PixelMode::White);
  driver.drawPixel(3, 9, PixelMode::Black);
  assert(driver.buffer()[kWidth + 3] == 0x00);

  transport.resetLog();
  assert(driver.sleep());
  assert(driver.wake());
  assert(driver.invert(true));
  assert(driver.invert(false));
  assert(transport.transactions.size() == 4);
  assert(transport.transactions[0].data == std::vector<uint8_t>{0xae});
  assert(transport.transactions[1].data == std::vector<uint8_t>{0xaf});
  assert(transport.transactions[2].data == std::vector<uint8_t>{0xa7});
  assert(transport.transactions[3].data == std::vector<uint8_t>{0xa6});

  driver.shutdown();
  assert(!driver.ready() && driver.buffer() == nullptr);
  assert(transport.transactions.back().data == std::vector<uint8_t>{0xae});

  FakeTransport externalTransport;
  Driver external(externalTransport);
  Config externalConfig;
  externalConfig.address = 0x3d;
  externalConfig.powerMode = PowerMode::ExternalVpp;
  externalConfig.contrast = 0x55;
  assert(external.begin(externalConfig));
  assert(externalTransport.probedAddress == 0x3d);
  assert(containsPair(externalTransport.transactions.front().data, 0xad, 0x8a));
  assert(containsPair(externalTransport.transactions.front().data, 0x81, 0x55));

  FakeTransport absentTransport;
  absentTransport.probeResult = false;
  Driver absent(absentTransport);
  assert(!absent.begin());
  assert(!absent.ready() && absent.buffer() == nullptr);
  assert(absentTransport.transactions.empty());

  FakeTransport failedTransport;
  failedTransport.failOnWrite = 3;
  Driver failed(failedTransport);
  assert(!failed.begin());
  assert(!failed.ready() && failed.buffer() == nullptr);

  FakeTransport invalidTransport;
  Driver invalid(invalidTransport);
  Config invalidAddress;
  invalidAddress.address = 0x3e;
  assert(!invalid.begin(invalidAddress));
  Config invalidOffset;
  invalidOffset.columnOffset = 5;
  assert(!invalid.begin(invalidOffset));
  assert(invalidTransport.writeCount == 0);
}
