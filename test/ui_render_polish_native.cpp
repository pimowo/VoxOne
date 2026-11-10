#include <cassert>
#include <cstdio>
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
}

int main() {
  const std::string display = readFile("src/core/display.cpp");
  const std::string displayHeader = readFile("src/core/display.h");
  const std::string oled = readFile(
      "src/displays/displayOLED128x64Runtime.cpp");
  const std::string multi = readFile("src/displays/displayMultiRuntime.cpp");
  const std::string common = readFile("src/displays/tools/commongfx.h");
  const std::string font = readFile("assets/yoradio_glcdfont.c");

  assert(display.find("voxone::utf8::ellipsize(") != std::string::npos);
  assert(display.find("dsp.setClipping({sideInset, 0, availableWidth") !=
         std::string::npos);
  assert(display.find("rowForeground);") != std::string::npos);

  assert(displayHeader.find("_yamahaTopSignature") != std::string::npos);
  assert(displayHeader.find("_yamahaMetadataSignature") != std::string::npos);
  assert(display.find("yamahaViewSignature") == std::string::npos);
  assert(display.find("if (!_yamahaDrawn) dsp.fillScreen(kBackground);") !=
         std::string::npos);
  assert(display.find("metadataSignature != _yamahaMetadataSignature") !=
         std::string::npos);
  assert(display.find("volumeSignature != _yamahaVolumeSignature") !=
         std::string::npos);

  assert(oled.find("if (!ready_ || (!dirty_ && !force)) return;") !=
         std::string::npos);
  assert(oled.find("dirty_ = false;") != std::string::npos);
  assert(oled.find("dirty_ = true;") != std::string::npos);
  assert(multi.find("oled_->display(force)") != std::string::npos);
  assert(common.find("flushSelected(force)") != std::string::npos);
  assert(common.find("display(force)") != std::string::npos);

  // The boot logo may be retired only after a forced first-frame commit.
  const std::size_t handoff = display.find(
      "display handoff\\tPLAYER committed");
  const std::size_t forcedFlush = display.rfind("dsp.loop(true);", handoff);
  const std::size_t ready = display.rfind("_bootStep = 2;", handoff);
  assert(handoff != std::string::npos && forcedFlush != std::string::npos);
  assert(ready != std::string::npos && forcedFlush < ready && ready < handoff);
  assert(display.find("_pager->removePage(_boot);\n    _boot = nullptr;") !=
         std::string::npos);

  // The audited project font already owns all 18 Polish lower/upper slots.
  for (unsigned slot = 0x80; slot <= 0x91; ++slot) {
    char marker[16];
    std::snprintf(marker, sizeof(marker), "// 0x%02X", slot);
    assert(font.find(marker) != std::string::npos);
  }
  return 0;
}
