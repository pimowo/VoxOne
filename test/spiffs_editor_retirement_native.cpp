#include "../src/core/config_format.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {
bool exists(const char* path) {
  std::ifstream input(path, std::ios::binary);
  return input.good();
}

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool contains(const std::string& source, const char* token) {
  return source.find(token) != std::string::npos;
}
}  // namespace

int main() {
  static_assert(voxone::config_format::kConfigV7SerializedSize == 255,
                "Config v7 layout changed");

  assert(!exists("src/AsyncWebServer/SPIFFSEditor.cpp"));
  assert(!exists("src/AsyncWebServer/SPIFFSEditor.h"));
  assert(!exists("src/AsyncWebServer/edit.htm"));

  const auto server = readFile("src/core/netserver.cpp");
  const auto serverHeader = readFile("src/core/netserver.h");
  assert(!contains(server, "SPIFFSEditor"));
  assert(!contains(serverHeader, "SPIFFSEditor"));
  assert(!contains(server, "\"/edit\""));
  assert(!contains(server, "new SPIFFSEditor"));

  assert(contains(server, "serveStatic(\"/\", SPIFFS, \"/www/\")"));
  assert(contains(serverHeader, "emptyfs_html"));
  assert(contains(serverHeader, "emergency_form"));
  assert(contains(server, "request->url()==\"/update\""));
  assert(contains(server, "request->url()==\"/emergency\""));
  assert(contains(server, "/api/stations/import"));
  assert(contains(server, "/api/stations/export"));
}
