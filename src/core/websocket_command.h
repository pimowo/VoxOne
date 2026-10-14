#ifndef VOXONE_WEBSOCKET_COMMAND_H
#define VOXONE_WEBSOCKET_COMMAND_H

#include <stddef.h>
#include <stdint.h>

namespace voxone {
namespace websocket {

enum class CommandParseStatus : uint8_t {
  Ok,
  Invalid,
  CommandTooLong,
  ValueTooLong
};

// WebSocket payloads are length-delimited and are not guaranteed to have a
// writable byte after the payload. Parse directly from (data, length) without
// adding a terminator to the transport-owned receive buffer.
CommandParseStatus parseCommand(const uint8_t* data, size_t length,
                                char* command, size_t commandCapacity,
                                char* value, size_t valueCapacity);

}  // namespace websocket
}  // namespace voxone

#endif
