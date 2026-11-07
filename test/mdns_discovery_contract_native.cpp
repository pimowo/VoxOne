#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::size_t requireAfter(const std::string& source, const char* token,
                         std::size_t previous) {
  const std::size_t position = source.find(token, previous);
  assert(position != std::string::npos);
  assert(position > previous);
  return position;
}
}  // namespace

int main() {
  const std::string source = readFile("src/core/network.cpp");
  std::size_t position = source.find("MDNS.begin(runtime.mdnsName)");
  assert(position != std::string::npos);

  position = requireAfter(
      source, "if (runtime.haDiscoveryEnabled) {", position);

  position = requireAfter(
      source, "MDNS.addService(\"http\", \"tcp\", 80);", position);
  position = requireAfter(
      source, "MDNS.addService(\"voxone\", \"tcp\", 80);", position);
  position = requireAfter(source,
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"mac\",\n"
      "                         mac_address_string.c_str());",
      position);
  position = requireAfter(source,
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"version\",\n"
      "                         firmware_version.c_str());",
      position);
  position = requireAfter(source,
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"model\", "
      "model_string.c_str());",
      position);
  position = requireAfter(source,
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"host\", "
      "runtime.mdnsName);",
      position);
  position = requireAfter(source,
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"name\", "
      "friendly_name.c_str());",
      position);
  position = requireAfter(source,
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"security\",\n"
      "                         is_protected ? \"protected\" : \"open\");",
      position);
  requireAfter(source,
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"manufacturer\", "
      "\"VoxOne\");",
      position);

  assert(source.find(
      "const String mac_address_string = WiFi.macAddress();") !=
      std::string::npos);
  assert(source.find(
      "const String model_string = \"VoxOne-\" + "
      "String(VOXONE_PROFILE_NAME);") != std::string::npos);
  assert(source.find(
      "const String firmware_version =\n"
      "        String(VOXONE_VERSION) + \"-\" + VOXONE_BUILD_CHANNEL;") !=
      std::string::npos);
  assert(source.find(
      "const String friendly_name = runtime.haName[0]") !=
      std::string::npos);
  assert(source.find(
      "? String(runtime.haName) : String(runtime.mdnsName);") !=
      std::string::npos);
  assert(source.find(
      "security.state == voxone::security::SecurityState::Protected") !=
      std::string::npos);

  assert(source.find(
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"profile\"") ==
      std::string::npos);

  assert(source.find(
      "strlen(runtime.mdnsName) > 0 && MDNS.begin(runtime.mdnsName)") !=
      std::string::npos);
}
