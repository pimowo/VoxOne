#ifndef VOXONE_UI_MENU_H
#define VOXONE_UI_MENU_H

#include <atomic>
#include <stdint.h>

namespace voxone {
namespace ui_menu {

enum class MenuState : uint8_t { CLOSED, OPEN, EDITING };

enum class MenuCommand : uint8_t {
  PREVIOUS,
  NEXT,
  SELECT,
  CLOSE,
};

enum class MenuAction : uint8_t {
  NONE,
  NAVIGATED,
  SELECTED,
  CLOSED,
};

struct MenuSnapshot {
  MenuState state;
  uint8_t selectedIndex;
  uint32_t revision;
};

class UiMenu {
 public:
  static constexpr uint8_t kItemCount = 4;

  bool open();
  bool close();
  bool visible() const;
  MenuState state() const;
  uint8_t selectedIndex() const;
  const char* selectedLabel() const;
  MenuSnapshot snapshot() const;
  MenuAction handle(MenuCommand command, int8_t steps = 1);

 private:
  void markChanged();

  std::atomic<MenuState> state_{MenuState::CLOSED};
  std::atomic<uint8_t> selectedIndex_{0};
  std::atomic<uint32_t> revision_{0};
};

const char* itemLabel(uint8_t index);

}  // namespace ui_menu
}  // namespace voxone

extern voxone::ui_menu::UiMenu uiMenu;

#endif
