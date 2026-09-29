#ifndef VOXONE_SOURCE_MANAGER_H
#define VOXONE_SOURCE_MANAGER_H

#include "options.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
void sourceManagerBegin();
void sourceManagerLoop();
#endif

#endif
