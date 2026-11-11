#include "../src/core/ui_menu.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>

int main() {
  using namespace voxone::ui_menu;
  UiMenu menu;
  assert(menu.open(MenuId::DLNA));

  std::atomic<bool> done{false};
  std::thread writer([&]() {
    for (unsigned generation = 1; generation <= 20000; ++generation) {
      DlnaMenuEntry entries[3]{};
      for (unsigned index = 0; index < 3; ++index) {
        std::snprintf(entries[index].label, sizeof(entries[index].label),
                      "%08u-%u", generation, index);
        entries[index].folder = index == 0;
        entries[index].playable = index != 0;
        entries[index].playing = index == 1;
      }
      menu.setDlnaItems(entries, 3);
    }
    done.store(true, std::memory_order_release);
  });

  do {
    const MenuSnapshot snapshot = menu.snapshot();
    const uint8_t count = itemCount(snapshot);
    if (count == 0) continue;
    assert(count == 3);
    unsigned generation = 0;
    unsigned item = 0;
    assert(std::sscanf(itemLabel(snapshot, 0), "%u-%u",
                       &generation, &item) == 2);
    assert(item == 0);
    for (uint8_t index = 0; index < count; ++index) {
      unsigned currentGeneration = 0;
      unsigned currentItem = 0;
      assert(std::sscanf(itemLabel(snapshot, index), "%u-%u",
                         &currentGeneration, &currentItem) == 2);
      assert(currentGeneration == generation);
      assert(currentItem == index);
    }
    assert(itemHasSubmenu(snapshot, 0));
    assert(itemIsActive(snapshot, 1));
  } while (!done.load(std::memory_order_acquire));

  writer.join();
  return 0;
}
