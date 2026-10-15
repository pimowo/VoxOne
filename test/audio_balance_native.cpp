#include "../src/core/audio_balance.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace balance = voxone::audio_balance;

namespace {

int16_t unpackLeft(int32_t packed) {
  return static_cast<int16_t>(static_cast<uint32_t>(packed) >> 16);
}

int16_t unpackRight(int32_t packed) {
  return static_cast<int16_t>(static_cast<uint32_t>(packed) & 0xffffu);
}

void testGainMatrix() {
  for (unsigned volume = 0; volume <= 254; ++volume) {
    uint8_t previousPositive = static_cast<uint8_t>(volume);
    uint8_t previousNegative = static_cast<uint8_t>(volume);
    for (int rawBalance = -16; rawBalance <= 16; ++rawBalance) {
      const balance::StereoGain gain = balance::calculateStereoGain(
          static_cast<uint8_t>(volume), static_cast<int8_t>(rawBalance));
      assert(gain.left <= volume);
      assert(gain.right <= volume);
      if (rawBalance == 0) {
        assert(gain.left == volume);
        assert(gain.right == volume);
      } else if (rawBalance < 0) {
        assert(gain.right == volume);
      } else {
        assert(gain.left == volume);
      }

      const balance::StereoGain mirror = balance::calculateStereoGain(
          static_cast<uint8_t>(volume), static_cast<int8_t>(-rawBalance));
      assert(gain.left == mirror.right);
      assert(gain.right == mirror.left);
      if (rawBalance > 0) {
        assert(gain.right <= previousPositive);
        previousPositive = gain.right;
      }
    }
    for (int magnitude = 1; magnitude <= 16; ++magnitude) {
      const balance::StereoGain negative = balance::calculateStereoGain(
          static_cast<uint8_t>(volume), static_cast<int8_t>(-magnitude));
      assert(negative.left <= previousNegative);
      previousNegative = negative.left;
    }

    const balance::StereoGain hardLeft = balance::calculateStereoGain(
        static_cast<uint8_t>(volume), -16);
    const balance::StereoGain hardRight = balance::calculateStereoGain(
        static_cast<uint8_t>(volume), 16);
    assert(hardLeft.left == 0 && hardLeft.right == volume);
    assert(hardRight.left == volume && hardRight.right == 0);
  }
}

void testPcmApplication() {
  const int16_t left = std::numeric_limits<int16_t>::min();
  const int16_t right = std::numeric_limits<int16_t>::max();

  const balance::StereoGain centered = balance::calculateStereoGain(254, 0);
  const int32_t centerPcm = balance::applyStereoGain(left, right, centered);
  assert(unpackLeft(centerPcm) == balance::scalePcmSample(left, 254));
  assert(unpackRight(centerPcm) == balance::scalePcmSample(right, 254));

  const int32_t leftMuted = balance::applyStereoGain(
      left, right, balance::calculateStereoGain(254, -16));
  assert(unpackLeft(leftMuted) == 0);
  assert(unpackRight(leftMuted) == balance::scalePcmSample(right, 254));

  const int32_t rightMuted = balance::applyStereoGain(
      left, right, balance::calculateStereoGain(254, 16));
  assert(unpackLeft(rightMuted) == balance::scalePcmSample(left, 254));
  assert(unpackRight(rightMuted) == 0);

  for (int rawBalance = -16; rawBalance <= 16; ++rawBalance) {
    const int32_t silent = balance::applyStereoGain(
        left, right, balance::calculateStereoGain(
                         0, static_cast<int8_t>(rawBalance)));
    assert(unpackLeft(silent) == 0);
    assert(unpackRight(silent) == 0);
  }

  const balance::StereoGain minus15 = balance::calculateStereoGain(254, -15);
  const balance::StereoGain plus15 = balance::calculateStereoGain(254, 15);
  assert(minus15.left == 14 && minus15.right == 254);
  assert(plus15.left == 254 && plus15.right == 14);
}

void testCommandParser() {
  assert(balance::isBalanceCommand("balance"));
  assert(balance::isBalanceCommand("ballance"));
  assert(!balance::isBalanceCommand(nullptr));
  assert(!balance::isBalanceCommand("Balance"));
  assert(!balance::isBalanceCommand("balancex"));

  const char* accepted[] = {"-16", "-15", "0", "+0", "+15", "+16",
                            "000", "-00", "+016"};
  const int8_t expected[] = {-16, -15, 0, 0, 15, 16, 0, 0, 16};
  for (unsigned i = 0; i < sizeof(accepted) / sizeof(accepted[0]); ++i) {
    int8_t parsed = 99;
    assert(balance::parseValue(accepted[i], parsed));
    assert(parsed == expected[i]);
  }

  const char* rejected[] = {"",     "+",    "-",      "text", "1.0",
                            "16x",  " 1",   "1 ",     "-17",  "17",
                            "240",  "--1",  "+-1",    "999999999999999999999"};
  int8_t parsed = 77;
  assert(!balance::parseValue(nullptr, parsed));
  assert(parsed == 77);
  for (const char* value : rejected) {
    parsed = 77;
    assert(!balance::parseValue(value, parsed));
    assert(parsed == 77);
  }

  // Both WebSocket spellings share this parser, so invalid input cannot reach
  // Config::setBalance and valid historical commands preserve their value.
  for (const char* command : {"balance", "ballance"}) {
    assert(balance::isBalanceCommand(command));
    for (int value = -16; value <= 16; ++value) {
      char text[5];
      std::snprintf(text, sizeof(text), "%d", value);
      parsed = 99;
      assert(balance::parseValue(text, parsed));
      assert(parsed == value);
    }
  }
}

}  // namespace

int main() {
  testGainMatrix();
  testPcmApplication();
  testCommandParser();
  std::puts("PASS audio_balance_native");
}
