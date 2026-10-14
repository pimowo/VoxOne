#include "websocket_command.h"

#include <cstring>

namespace voxone {
namespace websocket {

CommandParseStatus parseCommand(const uint8_t* data, size_t length,
                                char* command, size_t commandCapacity,
                                char* value, size_t valueCapacity) {
  if (command && commandCapacity) command[0] = '\0';
  if (value && valueCapacity) value[0] = '\0';
  if (!data || !command || !commandCapacity || !value || !valueCapacity)
    return CommandParseStatus::Invalid;

  size_t separator = 0;
  while (separator < length && data[separator] != '=') {
    if (data[separator] == '\0') return CommandParseStatus::Invalid;
    ++separator;
  }
  if (separator == 0 || separator == length)
    return CommandParseStatus::Invalid;

  const size_t valueLength = length - separator - 1;
  for (size_t index = separator + 1; index < length; ++index)
    if (data[index] == '\0') return CommandParseStatus::Invalid;
  if (separator >= commandCapacity)
    return CommandParseStatus::CommandTooLong;
  if (valueLength >= valueCapacity)
    return CommandParseStatus::ValueTooLong;

  std::memcpy(command, data, separator);
  command[separator] = '\0';
  if (valueLength) std::memcpy(value, data + separator + 1, valueLength);
  value[valueLength] = '\0';
  return CommandParseStatus::Ok;
}

}  // namespace websocket
}  // namespace voxone
