#ifndef VOXONE_SOURCE_MANAGER_H
#define VOXONE_SOURCE_MANAGER_H

#include "options.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
#include "bt_transport.h"

void sourceManagerBegin();
void sourceManagerLoop();
void cycleNextSource();
bool bluetoothSourceSelected();
bool bluetoothTransportAvailable();
void sourceManagerTransport(BtTransportInput input);
#endif

#endif
