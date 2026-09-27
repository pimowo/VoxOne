#ifndef VOXONE_VOLUME_MAP_H
#define VOXONE_VOLUME_MAP_H

#include <stdint.h>

// User-facing 0..100 is derived from the persisted raw 0..254 volume.
uint8_t volumeUserToRaw(uint8_t user);
uint8_t volumeRawToUser(uint8_t raw);

#endif
