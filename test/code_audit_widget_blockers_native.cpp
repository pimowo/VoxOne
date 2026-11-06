#include "../src/displays/widgets/owned_pointer_list.h"
#include "../src/displays/widgets/widget_memory.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <list>
#include <string>

namespace {

int allocations = 0;
int releases = 0;
int failAt = 0;

void* injectedCalloc(size_t count, size_t size) {
  ++allocations;
  if (failAt == allocations) return nullptr;
  return std::calloc(count, size);
}

void injectedFree(void* memory) {
  ++releases;
  std::free(memory);
}

struct FrameBufferProbe {
  bool value;
  bool ready() const { return value; }
};

struct OwnedProbe {
  static int destroyed;
  ~OwnedProbe() { ++destroyed; }
};
int OwnedProbe::destroyed = 0;

std::string readFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
  FrameBufferProbe notReady{false};
  FrameBufferProbe ready{true};
  assert(!voxone::display::frameBufferReady<FrameBufferProbe>(nullptr));
  assert(!voxone::display::frameBufferReady(&notReady));
  assert(voxone::display::frameBufferReady(&ready));

  allocations = releases = 0;
  failAt = 1;
  {
    voxone::display::WidgetBuffer buffer;
    assert(!buffer.allocate(32, injectedCalloc, injectedFree));
    assert(buffer.data() == nullptr && buffer.size() == 0);
  }
  assert(releases == 0);

  allocations = releases = 0;
  failAt = 0;
  {
    voxone::display::WidgetBuffer buffer;
    assert(buffer.allocate(32, injectedCalloc, injectedFree));
    assert(buffer.size() == 32);
    for (size_t i = 0; i < buffer.size(); ++i) assert(buffer.data()[i] == 0);
  }
  assert(releases == 1);

  std::list<OwnedProbe*> owned;
  owned.push_back(new OwnedProbe());
  owned.push_back(new OwnedProbe());
  owned.push_back(new OwnedProbe());
  voxone::display::deleteOwnedPointers(owned);
  assert(owned.empty() && OwnedProbe::destroyed == 3);

  const std::string widgets = readFile("src/displays/widgets/widgets.cpp");
  const std::string pages = readFile("src/displays/widgets/pages.cpp");
  const std::string display = readFile("src/core/display.cpp");
  assert(widgets.find("return voxone::display::frameBufferReady(_fb);") !=
         std::string::npos);
  assert(widgets.find("if(_fb->ready())") == std::string::npos);
  assert(widgets.find("_textBuffer.allocate(_buffsize)") != std::string::npos);
  assert(pages.find("deleteOwnedPointers(_widgets)") != std::string::npos);
  assert(pages.find("for (const auto& w : _widgets) removeWidget(w)") ==
         std::string::npos);
  assert(display.find("while(displayQueue==NULL)") == std::string::npos);
  assert(display.find("display queue allocation failed") != std::string::npos);
  assert(display.find("new (std::nothrow) Pager()") != std::string::npos);
}
