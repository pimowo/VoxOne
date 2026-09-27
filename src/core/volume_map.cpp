#include "volume_map.h"
namespace {
// round(254 * (user / 100)^1.3), with no runtime floating point.
constexpr uint8_t kUserToRaw[101] = {
0,1,2,3,4,5,7,8,10,11,13,14,16,18,20,22,23,25,27,29,
31,33,35,38,40,42,44,46,49,51,53,55,58,60,62,65,67,70,72,75,
77,80,82,85,87,90,93,95,98,100,103,106,109,111,114,117,120,122,125,128,
131,134,136,139,142,145,148,151,154,157,160,163,166,169,172,175,178,181,184,187,
190,193,196,199,202,206,209,212,215,218,221,225,228,231,234,238,241,244,247,251,254
};
}
uint8_t volumeUserToRaw(uint8_t user) {
  return kUserToRaw[user > 100 ? 100 : user];
}
uint8_t volumeRawToUser(uint8_t raw) {
  if (raw >= 254) return 100;
  uint8_t low = 0, high = 100;
  while (low < high) {
    const uint8_t middle = low + (high - low) / 2;
    if (kUserToRaw[middle] < raw) low = middle + 1;
    else high = middle;
  }
  if (low == 0) return 0;
  const uint8_t lower = low - 1;
  return raw - kUserToRaw[lower] <= kUserToRaw[low] - raw ? lower : low;
}
uint8_t volumeRawMaximum(uint8_t maximum) {
  return volumeUserToRaw(maximum > 100 ? 100 : maximum);
}
uint8_t volumeUserToRaw(uint8_t user, uint8_t maximum) {
  if (user > 100) user = 100;
  if (maximum > 100) maximum = 100;
  // Interpolate the existing gamma table at a fractional USER position.
  // A single final rounding preserves the original table for maximum=100.
  const uint16_t scaled = static_cast<uint16_t>(user) * maximum;
  const uint8_t index = scaled / 100;
  const uint8_t fraction = scaled % 100;
  if (index >= 100) return kUserToRaw[100];
  const uint16_t raw100 = static_cast<uint16_t>(kUserToRaw[index]) * (100 - fraction) +
                          static_cast<uint16_t>(kUserToRaw[index + 1]) * fraction;
  return (raw100 + 50) / 100;
}
uint8_t volumeRawToUser(uint8_t raw, uint8_t maximum) {
  const uint8_t ceiling = volumeRawMaximum(maximum);
  if (raw >= ceiling) return 100;
  uint8_t best = 0;
  uint8_t distance = 255;
  for (uint8_t user = 0; user <= 100; ++user) {
    const uint8_t mapped = volumeUserToRaw(user, maximum);
    const uint8_t delta = mapped > raw ? mapped - raw : raw - mapped;
    if (delta < distance) { best = user; distance = delta; }
  }
  return best;
}
VolumeState volumeStateFromUser(uint8_t user, uint8_t maximum) {
  if (user > 100) user = 100;
  return {volumeUserToRaw(user, maximum), user};
}
VolumeState volumeStateFromRaw(uint8_t raw, uint8_t maximum) {
  const uint8_t ceiling = volumeRawMaximum(maximum);
  if (raw > ceiling) raw = ceiling;
  return {raw, volumeRawToUser(raw, maximum)};
}
VolumeState volumeStateAfterMaximum(VolumeState current, uint8_t oldMaximum, uint8_t newMaximum) {
  if (newMaximum < oldMaximum) return volumeStateFromUser(current.user, newMaximum);
  return volumeStateFromRaw(current.raw, newMaximum);
}
VolumeState volumeStateAtStartup(uint8_t storedRaw, uint8_t lastUser, uint8_t maximum,
                                 bool fixed, uint8_t fixedUser, bool justMigrated) {
  if (fixed) return volumeStateFromUser(fixedUser, maximum);
  if (justMigrated) return {storedRaw, lastUser};
  return volumeStateFromUser(lastUser, maximum);
}
uint8_t volumeClampOutput(int output, uint8_t maximum) {
  if (output < 0) return 0;
  const uint8_t ceiling = volumeRawMaximum(maximum);
  if (output > ceiling) output = ceiling;
  return static_cast<uint8_t>(output);
}
