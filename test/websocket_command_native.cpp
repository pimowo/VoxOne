#include "../src/core/websocket_command.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using voxone::websocket::CommandParseStatus;
using voxone::websocket::parseCommand;

namespace {

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void testExactLengthPayloadNeedsNoWritableSentinel() {
  const std::vector<uint8_t> payload{'p', 'i', 'n', 'g', '=', '1'};
  const std::vector<uint8_t> original = payload;
  char command[65];
  char value[65];
  assert(parseCommand(payload.data(), payload.size(), command,
                      sizeof(command), value, sizeof(value)) ==
         CommandParseStatus::Ok);
  assert(std::strcmp(command, "ping") == 0);
  assert(std::strcmp(value, "1") == 0);
  assert(payload == original);
}

void testBoundsAndMalformedFrames() {
  char command[65];
  char value[65];
  const std::string maximum = std::string(64, 'c') + "=" +
                              std::string(64, 'v');
  assert(parseCommand(reinterpret_cast<const uint8_t*>(maximum.data()),
                      maximum.size(), command, sizeof(command), value,
                      sizeof(value)) == CommandParseStatus::Ok);
  assert(std::strlen(command) == 64 && std::strlen(value) == 64);

  const std::string commandTooLong = std::string(65, 'c') + "=v";
  assert(parseCommand(
             reinterpret_cast<const uint8_t*>(commandTooLong.data()),
             commandTooLong.size(), command, sizeof(command), value,
             sizeof(value)) == CommandParseStatus::CommandTooLong);
  assert(command[0] == '\0' && value[0] == '\0');

  const std::string valueTooLong = "c=" + std::string(65, 'v');
  assert(parseCommand(reinterpret_cast<const uint8_t*>(valueTooLong.data()),
                      valueTooLong.size(), command, sizeof(command), value,
                      sizeof(value)) == CommandParseStatus::ValueTooLong);
  assert(command[0] == '\0' && value[0] == '\0');

  const uint8_t malformed[] = {'p', 'i', 'n', 'g'};
  assert(parseCommand(malformed, sizeof(malformed), command,
                      sizeof(command), value, sizeof(value)) ==
         CommandParseStatus::Invalid);
  assert(parseCommand(nullptr, 0, command, sizeof(command), value,
                      sizeof(value)) == CommandParseStatus::Invalid);

  const uint8_t embeddedCommandNul[] = {'p', 'i', 'n', 'g', '\0', 'x', '=', '1'};
  assert(parseCommand(embeddedCommandNul, sizeof(embeddedCommandNul), command,
                      sizeof(command), value, sizeof(value)) ==
         CommandParseStatus::Invalid);
  const uint8_t embeddedValueNul[] = {'p', 'i', 'n', 'g', '=', '1', '\0', 'x'};
  assert(parseCommand(embeddedValueNul, sizeof(embeddedValueNul), command,
                      sizeof(command), value, sizeof(value)) ==
         CommandParseStatus::Invalid);
}

void testRapidParseAndCleanupChurn() {
  const std::vector<uint8_t> payload{'g', 'e', 't', 'i', 'n', 'd', 'e', 'x',
                                     '=', '1'};
  for (size_t iteration = 0; iteration < 100000; ++iteration) {
    char command[65];
    char value[65];
    std::string response(64 + (iteration % 128), 'r');
    assert(parseCommand(payload.data(), payload.size(), command,
                        sizeof(command), value, sizeof(value)) ==
           CommandParseStatus::Ok);
    assert(command[0] == 'g' && value[0] == '1');
    response.clear();
  }
}

void testProductionSourcesUseLengthBoundedContract() {
  const std::string websocket =
      readFile("src/AsyncWebServer/AsyncWebSocket.cpp");
  const std::string netserver = readFile("src/core/netserver.cpp");
  assert(websocket.find("data[datalen]") == std::string::npos);
  assert(websocket.find("strlen(reasonString)") == std::string::npos);
  assert(websocket.find("memcpy(_data, data, _len)") != std::string::npos);
  assert(netserver.find("data[len] = 0") == std::string::npos);
  assert(netserver.find("websocket::parseCommand(") != std::string::npos);
}

}  // namespace

int main() {
  testExactLengthPayloadNeedsNoWritableSentinel();
  testBoundsAndMalformedFrames();
  testRapidParseAndCleanupChurn();
  testProductionSourcesUseLengthBoundedContract();
  return 0;
}
