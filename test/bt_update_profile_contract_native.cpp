#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}
}

int main() {
  const std::string s0 = readFile("profiles/s0.h");
  const std::string l0 = readFile("profiles/l0.h");
  const std::string netserver = readFile("src/core/netserver.cpp");
  const std::string staging = readFile("src/core/bt_firmware_staging.cpp");

  assert(s0.find("#define VOXONE_HAS_BT 1") != std::string::npos);
  assert(s0.find("#define VOXONE_BT_FW_UPDATE_ENABLED 0") !=
         std::string::npos);
  assert(l0.find("#define VOXONE_BT_FW_UPDATE_ENABLED 1") !=
         std::string::npos);
  assert(netserver.find(
      "VOXONE_PIN_MAP_COMPLETE && VOXONE_BT_FW_UPDATE_ENABLED") !=
      std::string::npos);
  assert(staging.find(
      "#if !defined(ARDUINO) || VOXONE_BT_FW_UPDATE_ENABLED") !=
      std::string::npos);
  assert(netserver.find("#if VOXONE_BT_UPDATE_UPLOAD") !=
         std::string::npos);
}
