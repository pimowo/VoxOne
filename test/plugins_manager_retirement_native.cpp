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
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool contains(const std::string& source, const char* token) {
  return source.find(token) != std::string::npos;
}
}  // namespace

int main() {
  assert(!exists("src/pluginsManager/pluginsManager.cpp"));
  assert(!exists("src/pluginsManager/pluginsManager.h"));
  assert(!exists("src/pluginsManager/README.md"));
  assert(!exists("src/plugins/README.md"));

  std::string activeSources;
  for (const char* path : {"src/main.cpp", "src/core/network.cpp",
                           "src/core/player.cpp", "src/core/display.cpp",
                           "src/core/timekeeper.cpp", "src/core/controls.cpp"}) {
    activeSources += readFile(path);
  }

  for (const char* token : {"pluginsManager", "class Plugin", "registerPlugin",
                            "pm.on_", "pm.add(", "pm.get(", "pm.count("}) {
    assert(!contains(activeSources, token));
  }

  const auto display = readFile("src/core/display.cpp");
  assert(!contains(display, "pm_result"));
  assert(contains(display, "switch (request.type)"));
}
