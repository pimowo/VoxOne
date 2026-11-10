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

std::string functionBody(const std::string& source, const char* signature,
                         const char* nextSignature) {
  const size_t begin = source.find(signature);
  assert(begin != std::string::npos);
  const size_t end = source.find(nextSignature, begin + 1);
  assert(end != std::string::npos);
  return source.substr(begin, end - begin);
}
}  // namespace

int main() {
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string chunked = functionBody(
      server, "size_t NetServer::chunkedHtmlPageCallback(",
      "void NetServer::chunkedHtmlPage(");
  const size_t openFailure = chunked.find("if (!requiredfile)");
  const size_t openFailureUnlock = chunked.find("display.unlock()", openFailure);
  assert(openFailure != std::string::npos);
  assert(openFailureUnlock != std::string::npos);
  assert(chunked.find("index >= filesize") != std::string::npos);
  assert(chunked.find("!requiredfile.seek(index, SeekSet)") !=
         std::string::npos);
  assert(chunked.find("const size_t bytesRead") != std::string::npos);

  const std::string display = readFile("src/core/display.cpp");
  assert(display.find("void Display::_removeBootPage()") != std::string::npos);
  assert(display.find("!_pager->removePage(_boot)) delete _boot") !=
         std::string::npos);
  assert(display.find("void drawEmpty(") != std::string::npos);
  assert(display.find("Brak elementów") != std::string::npos);

  const std::string power = readFile("src/core/power_manager.cpp");
  const std::string longPress = functionBody(
      power, "ButtonEvent::BUTTON_LONG_PRESS", "if (shutdownNotice_");
  assert(longPress.find("uiState.mode() == PLAYER") != std::string::npos);
  assert(longPress.find("dispatchUiInput(UiInputEvent::Long)") !=
         std::string::npos);

  const std::string manager = readFile("src/core/source_manager.cpp");
  assert(manager.find("uiState") == std::string::npos);
  assert(manager.find("EVENT_SOURCE_CHANGED") != std::string::npos);
  return 0;
}
