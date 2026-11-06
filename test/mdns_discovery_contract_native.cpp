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
      source, "MDNS.addService(\"http\", \"tcp\", 80);", position);
  position = requireAfter(
      source, "MDNS.addService(\"voxone\", \"tcp\", 80);", position);
  position = requireAfter(source,
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"model\", \"VoxOne\");",
      position);
  position = requireAfter(source,
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"version\", "
      "VOXONE_VERSION);",
      position);
  position = requireAfter(source,
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"mac\", "
      "WiFi.macAddress().c_str());",
      position);
  requireAfter(source,
      "MDNS.addServiceTxt(\"voxone\", \"tcp\", \"profile\", "
      "VOXONE_PROFILE_NAME);",
      position);

  assert(source.find(
      "strlen(runtime.mdnsName) > 0 && MDNS.begin(runtime.mdnsName)") !=
      std::string::npos);
}
