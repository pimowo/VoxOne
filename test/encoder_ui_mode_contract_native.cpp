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
}  // namespace

int main() {
  const std::string config = readFile("src/core/config.cpp");
  const std::string controls = readFile("src/core/controls.cpp");
  const std::string input = readFile("src/core/ui_input.cpp");
  const std::string menu = readFile("src/core/ui_menu.cpp");
  const std::string command = readFile("src/core/commandhandler.cpp");
  const std::string server = readFile("src/core/netserver.cpp");
  const std::string html = readFile("web-src/voxone.html");
  const std::string javascript = readFile("web-src/voxone.js");

  assert(config.find("encoder_ui::decode") != std::string::npos);
  assert(config.find("setEncoderUiReversed") != std::string::npos);
  assert(config.find("uiMenu.configureEncoderUiReversed") !=
         std::string::npos);
  assert(controls.find("normalizeEc11Delta(") != std::string::npos);
  assert(controls.find("UiInputControl::EncoderGesture") !=
         std::string::npos);
  assert(input.find("runtime.encoderUiReversed") !=
         std::string::npos);
  assert(input.find("runtime.lineOutEnabled") != std::string::npos);
  assert(input.find("SourceId::Bluetooth") != std::string::npos);
  assert(input.find("SourceId::Dlna") != std::string::npos);
  assert(menu.find("Odwrócony enkoder") != std::string::npos);
  assert(command.find("encoderuireversed") != std::string::npos);
  assert(server.find("\\\"encoderUiReversed\\\":%s") !=
         std::string::npos);
  assert(html.find("Odwrócony enkoder (Obrót = utwory/stacje)") !=
         std::string::npos);
  assert(javascript.find("send(\"encoderuireversed\"") !=
         std::string::npos);
  assert(javascript.find("message.encoderUiReversed") !=
         std::string::npos);
}
