#ifndef VOXONE_VOLUME_MAP_H
#define VOXONE_VOLUME_MAP_H

#include <stdint.h>

uint8_t volumeUserToRaw(uint8_t user);
uint8_t volumeRawToUser(uint8_t raw);
uint8_t volumeUserToRaw(uint8_t user, uint8_t maximum);
uint8_t volumeRawToUser(uint8_t raw, uint8_t maximum);
uint8_t volumeRawMaximum(uint8_t maximum);

struct VolumeState {
  uint8_t raw;
  uint8_t user;
};
VolumeState volumeStateFromUser(uint8_t user, uint8_t maximum);
VolumeState volumeStateFromRaw(uint8_t raw, uint8_t maximum);
VolumeState volumeStateAfterMaximum(VolumeState current, uint8_t oldMaximum, uint8_t newMaximum);
VolumeState volumeStateAtStartup(uint8_t storedRaw, uint8_t lastUser, uint8_t maximum,
                                 bool fixed, uint8_t fixedUser, bool justMigrated);
uint8_t volumeClampOutput(int output, uint8_t maximum);

#endif
