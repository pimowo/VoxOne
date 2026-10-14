#include "../src/displays/widgets/scroll_text_state.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

struct GuardedWindow {
  unsigned char before = 0xA5;
  char data[8]{};
  unsigned char after = 0x5A;
};

}  // namespace

int main() {
  // Invalid dimensions are inert and never divide by zero.
  assert(!scrollTextNeedsScrolling(100, 0, 6, 8));
  assert(!scrollTextNeedsScrolling(100, 60, 0, 8));
  assert(!scrollTextNeedsScrolling(100, 60, 6, 0));
  assert(!scrollTextNeedsScrolling(60, 60, 6, 8));
  assert(scrollTextNeedsScrolling(61, 60, 6, 8));
  assert(scrollWindowCapacity(0, 0) == 2);
  assert(scrollWindowCapacity(std::numeric_limits<size_t>::max(), 1) ==
         std::numeric_limits<size_t>::max());
  assert(scrollWindowPrintCapacity(0, 60, 6, 1) == 0);
  assert(scrollWindowPrintCapacity(8, 0, 6, 0) == 1);
  assert(scrollWindowPrintCapacity(8, 60, 0, 1) == 1);
  assert(scrollWindowPrintCapacity(8, std::numeric_limits<size_t>::max(),
                                   1, std::numeric_limits<size_t>::max()) == 8);

  // Empty, short, equal-width and long metadata use deterministic widths.
  assert(scrollTextPixelWidth(0, 6) == 0);
  assert(scrollTextPixelWidth(9, 6) == 54);
  assert(scrollTextPixelWidth(10, 6) == 60);
  assert(scrollTextPixelWidth(11, 6) == 66);
  assert(scrollTextPixelWidth(std::numeric_limits<size_t>::max(), 255) ==
         std::numeric_limits<uint16_t>::max());
  assert(scrollCyclePixelWidth(0, 0) == 0);
  assert(scrollCyclePixelWidth(60, 18) == 78);
  assert(scrollCyclePixelWidth(std::numeric_limits<uint16_t>::max(), 1) ==
         std::numeric_limits<uint16_t>::max());
  assert(scrollCyclePixelWidth(65000, 1000) ==
         std::numeric_limits<uint16_t>::max());

  int16_t coordinate = -1;
  assert(scrollAbsoluteCoordinate(2, 0, 78, coordinate) && coordinate == 2);
  assert(scrollAbsoluteCoordinate(2, 1, 78, coordinate) && coordinate == 1);
  assert(scrollAbsoluteCoordinate(2, 77, 78, coordinate) && coordinate == -75);
  assert(!scrollAbsoluteCoordinate(2, 78, 78, coordinate) && coordinate == 2);
  assert(!scrollAbsoluteCoordinate(2, 1, 0, coordinate) && coordinate == 2);
  assert(!scrollAbsoluteCoordinate(0, 65534, 65535, coordinate) &&
         coordinate == 0);

  char text[8]{};
  int16_t offset = -123;
  assert(!scrollTextAssign(text, sizeof(text), "", offset, 4));
  assert(offset == -123);
  assert(scrollTextAssign(text, sizeof(text), "abc", offset, 4));
  assert(std::strcmp(text, "abc") == 0 && offset == 4);
  offset = -20;
  assert(!scrollTextAssign(text, sizeof(text), "abc", offset, 4));
  assert(offset == -20);  // Repeated metadata does not restart scrolling.

  // Oversized metadata keeps capacity-1 bytes, remains terminated and
  // compares the stored representation on repeated updates.
  assert(scrollTextAssign(text, sizeof(text), "abcdefghijklmnop", offset, 2));
  assert(std::strcmp(text, "abcdefg") == 0 && offset == 2);
  offset = -40;
  assert(!scrollTextAssign(text, sizeof(text), "abcdefghijklmnop", offset, 2));
  assert(offset == -40);
  assert(scrollTextAssign(text, sizeof(text), "x", offset, 7));
  assert(std::strcmp(text, "x") == 0 && offset == 7);
  assert(scrollTextAssign(text, sizeof(text), "abcdefg", offset, 1));
  assert(std::strcmp(text, "abcdefg") == 0);
  assert(scrollTextAssign(text, sizeof(text), "", offset, 9));
  assert(text[0] == '\0' && offset == 9);

  std::string veryLong(8192, 'M');
  char metadata[140]{};
  assert(scrollTextAssign(metadata, sizeof(metadata), veryLong.c_str(), offset, 3));
  assert(std::strlen(metadata) == sizeof(metadata) - 1);
  assert(metadata[sizeof(metadata) - 1] == '\0');
  offset = -99;
  assert(!scrollTextAssign(metadata, sizeof(metadata), veryLong.c_str(), offset, 3));
  assert(offset == -99);

  char one[1] = {'\0'};
  offset = 11;
  assert(!scrollTextAssign(one, sizeof(one), "oversized", offset, 0));
  assert(one[0] == '\0' && offset == 11);
  assert(!scrollTextAssign(nullptr, 0, "text", offset, 0));
  assert(!scrollTextAssign(text, sizeof(text), nullptr, offset, 0));

  // Project-local Polish glyphs are single display bytes after utf8Rus().
  // Copying and composing them must not treat high-bit values as delimiters.
  const char polishGlyphs[] = {static_cast<char>(0x91), static_cast<char>(0x8A),
                               static_cast<char>(0x86), static_cast<char>(0x82),
                               '\0'};  // Żółć in the local font encoding.
  assert(scrollTextAssign(text, sizeof(text), polishGlyphs, offset, 0));
  assert(std::memcmp(text, polishGlyphs, sizeof(polishGlyphs)) == 0);

  GuardedWindow guarded;
  assert(scrollComposeWindow(guarded.data, sizeof(guarded.data), "abcdef",
                             " * ", 0) == 7);
  assert(std::strcmp(guarded.data, "abcdef ") == 0);
  assert(guarded.before == 0xA5 && guarded.after == 0x5A);
  assert(scrollComposeWindow(guarded.data, sizeof(guarded.data), "abcdef",
                             " * ", 3) == 7);
  assert(std::strcmp(guarded.data, "def * a") == 0);
  assert(guarded.before == 0xA5 && guarded.after == 0x5A);

  // Crossing the separator used to form a pointer beyond the text array.
  assert(scrollComposeWindow(guarded.data, sizeof(guarded.data), "abcdef",
                             " * ", 6) == 7);
  assert(std::strcmp(guarded.data, " * abcd") == 0);
  assert(scrollComposeWindow(guarded.data, sizeof(guarded.data), "abcdef",
                             " * ", 7) == 7);
  assert(std::strcmp(guarded.data, "* abcde") == 0);
  assert(scrollComposeWindow(guarded.data, sizeof(guarded.data), "abcdef",
                             " * ", 8) == 7);
  assert(std::strcmp(guarded.data, " abcdef") == 0);
  assert(scrollComposeWindow(guarded.data, sizeof(guarded.data), "abcdef",
                             " * ", 9) == 6);
  assert(std::strcmp(guarded.data, "abcdef") == 0);
  assert(scrollComposeWindow(guarded.data, sizeof(guarded.data), "abcdef",
                             " * ", 9999) == 6);
  assert(std::strcmp(guarded.data, "abcdef") == 0);
  assert(guarded.before == 0xA5 && guarded.after == 0x5A);

  char emptyWindow[2] = {'#', '#'};
  assert(scrollComposeWindow(emptyWindow, sizeof(emptyWindow), "", "", 0) == 0);
  assert(emptyWindow[0] == '\0' && emptyWindow[1] == '#');
  assert(scrollComposeWindow(nullptr, 0, "abc", " * ", 0) == 0);

  // The production integration avoids null-member calls and matches object
  // allocation/deallocation while retaining the established timing fields.
  const std::string widgets = readFile("src/displays/widgets/widgets.cpp");
  const size_t scrollStart = widgets.find("SCROLL WIDGET");
  const size_t scrollEnd = widgets.find("SLIDER WIDGET", scrollStart);
  assert(scrollStart != std::string::npos && scrollEnd != std::string::npos);
  const std::string scroll = widgets.substr(scrollStart, scrollEnd - scrollStart);
  assert(scroll.find("delete _fb;") != std::string::npos);
  assert(scroll.find("return _fb && _fb->ready();") != std::string::npos);
  assert(scroll.find("if(_fb->ready())") == std::string::npos);
  assert(scroll.find("scrollTextAssign(") != std::string::npos);
  assert(scroll.find("scrollComposeWindow(") != std::string::npos);
  assert(scroll.find("_startscrolldelay = conf.startscrolldelay;") !=
         std::string::npos);
  assert(scroll.find("_scrolldelta = conf.scrolldelta;") != std::string::npos);
  assert(scroll.find("_scrolltime = conf.scrolltime;") != std::string::npos);
  assert(scroll.find("if (_coreControlled) return;") != std::string::npos);
  assert(scroll.find("ScrollWidget::coreScrollMetrics() const") !=
         std::string::npos);
  assert(scroll.find("ScrollWidget::renderScrollOffset(uint16_t offsetPixels)") !=
         std::string::npos);
  assert(scroll.find("scrollAbsoluteCoordinate(") != std::string::npos);
  assert(scroll.find("dsp.getScrollId() == this") != std::string::npos);
  assert(scroll.find("strcmp(_oldtext, _text) == 0") != std::string::npos);
}
