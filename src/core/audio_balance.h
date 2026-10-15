#pragma once

#include <cstdint>
#include <cstring>

namespace voxone {
namespace audio_balance {

constexpr int8_t kMinimum = -16;
constexpr int8_t kMaximum = 16;

struct StereoGain {
  uint8_t left;
  uint8_t right;
};

inline StereoGain calculateStereoGain(uint8_t volume, int8_t balance) {
  const int16_t signedBalance = balance;
  const uint16_t magnitude = static_cast<uint16_t>(
      signedBalance < 0 ? -signedBalance : signedBalance);
  uint32_t attenuation = static_cast<uint32_t>(volume) * magnitude * 16u /
                         254u;
  if (attenuation > volume) attenuation = volume;

  StereoGain gain{volume, volume};
  if (signedBalance < 0)
    gain.left = static_cast<uint8_t>(volume - attenuation);
  else if (signedBalance > 0)
    gain.right = static_cast<uint8_t>(volume - attenuation);
  return gain;
}

inline int32_t scalePcmSample(int16_t sample, uint8_t gain) {
  return (static_cast<int32_t>(sample) * gain) >> 8;
}

inline int32_t applyStereoGain(int16_t left, int16_t right,
                               const StereoGain& gain) {
  const int32_t scaledLeft = scalePcmSample(left, gain.left);
  const int32_t scaledRight = scalePcmSample(right, gain.right);
  const uint32_t packed =
      (static_cast<uint32_t>(static_cast<uint16_t>(scaledLeft)) << 16) |
      static_cast<uint16_t>(scaledRight);
  return static_cast<int32_t>(packed);
}

inline bool isBalanceCommand(const char* command) {
  return command &&
         (std::strcmp(command, "balance") == 0 ||
          std::strcmp(command, "ballance") == 0);
}

inline bool parseValue(const char* value, int8_t& parsed) {
  if (!value || !*value) return false;

  bool negative = false;
  if (*value == '+' || *value == '-') {
    negative = *value == '-';
    ++value;
  }
  if (*value < '0' || *value > '9') return false;

  uint8_t magnitude = 0;
  do {
    if (*value < '0' || *value > '9') return false;
    const uint8_t digit = static_cast<uint8_t>(*value - '0');
    if (magnitude > 1 || (magnitude == 1 && digit > 6)) return false;
    magnitude = static_cast<uint8_t>(magnitude * 10u + digit);
    ++value;
  } while (*value);

  const int16_t signedValue = negative ? -magnitude : magnitude;
  if (signedValue < kMinimum || signedValue > kMaximum) return false;
  parsed = static_cast<int8_t>(signedValue);
  return true;
}

}  // namespace audio_balance
}  // namespace voxone
