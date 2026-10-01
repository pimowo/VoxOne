#ifndef VOXONE_WEB_TRANSPORT_H
#define VOXONE_WEB_TRANSPORT_H

#include <stdint.h>
#include <string.h>

enum class WebTransportAction : uint8_t {
  None, RadioPrevious, RadioToggle, RadioNext,
  BluetoothPrevious, BluetoothToggle, BluetoothNext
};

inline WebTransportAction webTransportAction(bool bluetoothSelected,
                                              const char* command) {
  if (!command) return WebTransportAction::None;
  if (strcmp(command, "prev") == 0)
    return bluetoothSelected ? WebTransportAction::BluetoothPrevious
                             : WebTransportAction::RadioPrevious;
  if (strcmp(command, "toggle") == 0)
    return bluetoothSelected ? WebTransportAction::BluetoothToggle
                             : WebTransportAction::RadioToggle;
  if (strcmp(command, "next") == 0)
    return bluetoothSelected ? WebTransportAction::BluetoothNext
                             : WebTransportAction::RadioNext;
  return WebTransportAction::None;
}

#endif
