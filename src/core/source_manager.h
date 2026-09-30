#ifndef VOXONE_SOURCE_MANAGER_H
#define VOXONE_SOURCE_MANAGER_H

#include "options.h"

#if VOXONE_HAS_BT && VOXONE_PIN_MAP_COMPLETE
#include "bt_transport.h"

void sourceManagerBegin();
void sourceManagerLoop();
void sourceManagerStopForUpdate();
void cycleNextSource();
bool bluetoothSourceSelected();
bool bluetoothPhysicallyConnected();
bool radioI2SOutputEnabled();
#if VOXONE_BT_I2S_RX_ENABLED
uint16_t sourceManagerGetVuLevel(uint16_t dimension, bool& playing);
#endif
bool bluetoothTransportAvailable();
bool sourceManagerStepBluetoothVolume(int8_t delta);
void sourceManagerTransport(BtTransportInput input);
#endif

#endif
